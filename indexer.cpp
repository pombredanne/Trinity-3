#include "indexer.h"
#include "docidupdates.h"
#include <text.h>

using namespace Trinity;

void SegmentIndexSession::document_proxy::insert(const uint32_t termID, const uint32_t position, range_base<const uint8_t *, const uint8_t> payload)
{
        require(termID);

        if (const auto size = payload.size())
        {
                const auto l = hitsBuf.size();

                Drequire(size <= sizeof(uint64_t));
                hitsBuf.Serialize(payload.start(), size);
                hits.push_back({termID, {position, {l, size}}});
        }
        else
                hits.push_back({termID, {position, {0, 0}}});
}

void SegmentIndexSession::commit_document_impl(const document_proxy &proxy, const bool isUpdate)
{
        uint32_t terms{0};
        const auto all_hits = reinterpret_cast<const uint8_t *>(hitsBuf.data());

        std::sort(hits.begin(), hits.end(), [](const auto &a, const auto &b) {
                return a.first < b.first || (a.first == b.first && a.second.first < b.second.first);
        });

        b.pack(proxy.did);

        if (isUpdate)
        {
                updatedDocumentIDs.push_back(proxy.did);
        }

        const auto offset = b.size();

        b.pack(uint16_t(0));

        for (const auto *p = hits.data(), *const e = p + hits.size(); p != e;)
        {
                const auto term = p->first;
                uint32_t termHits{0};
                uint32_t prev{0};
                uint8_t prevPayloadSize{0xff};

                require(term);
                b.pack(term);

                const auto o = b.size();

                b.pack(uint16_t(0));

                do
                {
                        const auto &it = p->second;
                        const auto delta = it.first - prev;
                        const auto payloadSize = it.second.size();

                        prev = it.first;
                        if (payloadSize != prevPayloadSize)
                        {
                                b.SerializeVarUInt32((delta << 1) | 0);
                                b.SerializeVarUInt32(payloadSize);
                                prevPayloadSize = payloadSize;
                        }
                        else
                        {
                                // Same paload size
                                b.SerializeVarUInt32((delta << 1) | 1);
                        }

                        if (payloadSize)
                                b.Serialize(all_hits + it.second.start(), payloadSize);

                        ++termHits;
                } while (++p != e && p->first == term);

                require(termHits <= UINT16_MAX);
                *(uint16_t *)(b.data() + o) = termHits; // total hits for (document, term): TODO use varint?

                ++terms;
        }

        *(uint16_t *)(b.data() + offset) = terms; // total distinct terms for (document)

        if (b.size() > 16 * 1024 * 1024)
        {
                // flush buffer?
        }
}

uint32_t SegmentIndexSession::term_id(const strwlen8_t term)
{
	// indexer words space
	// Each segment has its own terms and there is no need to maintain a global(index) or local(segment) (term=>id) dictionary
	// but we use transient term IDs (integers). See IMPL.md
        uint32_t *idp;
        strwlen8_t *keyptr;

        if (dictionary.AddNeedKey(term, 0, keyptr, &idp))
        {
                keyptr->Set(dictionaryAllocator.CopyOf(term.data(), term.size()), term.size());
                *idp = dictionary.size();
		invDict.insert({*idp, *keyptr});
        }
        return *idp;
}

void SegmentIndexSession::erase(const uint32_t documentID)
{
        updatedDocumentIDs.push_back(documentID);
        b.pack(documentID, uint16_t(0));
}

Trinity::SegmentIndexSession::document_proxy SegmentIndexSession::begin(const uint32_t documentID)
{
        hits.clear();
        hitsBuf.clear();
        return {*this, documentID, hits, hitsBuf};
}

void SegmentIndexSession::commit(Trinity::Codecs::IndexSession *const sess)
{
        struct segment_data
        {
                uint32_t termID;
                uint32_t documentID;
                uint32_t hitsOffset;
                uint16_t hitsCnt;
        };

        std::vector<uint32_t> allOffsets;
        const auto termOffsetsCnt = sess->dictionary_offsets_count();
        Switch::unordered_map<uint32_t, uint32_t> offsetsMap;
	std::unique_ptr<Trinity::Codecs::Encoder> enc_(sess->new_encoder(sess));
	IOBuffer maskedProductsBuf;

        const auto scan = [enc = enc_.get(), &allOffsets, termOffsetsCnt, &offsetsMap](const auto data, const auto dataSize) {
                uint8_t payloadSize;
                std::vector<segment_data> all;
                uint32_t offsets[3];
		term_segment_ctx tctx;

                for (const auto *p = data, *const e = p + dataSize; p != e;)
                {
                        const auto documentID = *(uint32_t *)p;
                        p += sizeof(uint32_t);
                        auto termsCnt = *(uint16_t *)p;
                        p += sizeof(uint16_t);

                        if (!termsCnt)
                        {
                                // deleted?
                                continue;
                        }

                        do
                        {
                                const auto term = *(uint32_t *)p;
                                p += sizeof(uint32_t);
                                auto hitsCnt = *(uint16_t *)p;
                                const auto saved{hitsCnt};

                                p += sizeof(uint16_t);

                                const auto base{p};
                                do
                                {
                                        const auto deltaMask = Compression::UnpackUInt32(p);

                                        if (0 == (deltaMask & 1))
                                                payloadSize = Compression::UnpackUInt32(p);

                                        p += payloadSize;
                                } while (--hitsCnt);

                                all.push_back({term, documentID, uint32_t(base - data), saved});
                        } while (--termsCnt);
                }

                std::sort(all.begin(), all.end(), [](const auto &a, const auto &b) { return a.termID < b.termID; });

                for (const auto *it = all.data(), *const e = it + all.size(); it != e;)
                {
                        const auto term = it->termID;

                        enc->begin_term();

                        do
                        {
                                const auto documentID = it->documentID;
                                const auto hitsCnt = it->hitsCnt;
                                const auto *p = data + it->hitsOffset;
                                uint32_t pos{0};

                                enc->begin_document(documentID, hitsCnt);
                                for (uint32_t i{0}; i != hitsCnt; ++i)
                                {
                                        const auto deltaMask = Compression::UnpackUInt32(p);

                                        if (0 == (deltaMask & 1))
                                                payloadSize = Compression::UnpackUInt32(p);

                                        pos += deltaMask >> 1;

                                        enc->new_hit(pos, {p, payloadSize});

                                        p += payloadSize;
                                }
                                enc->end_document();

                        } while (++it != e && it->termID == term);

                        enc->end_term(&tctx);
                        offsetsMap.insert({term, (uint32_t)allOffsets.size()});
                        allOffsets.insert(allOffsets.end(), offsets, offsets + termOffsetsCnt);
                }
        };


        sess->begin();

        // IF we flushed b earlier, mmap() and scan() that mampped region first
        scan(reinterpret_cast<const uint8_t *>(b.data()), b.size());


	// Persist index
        int fd = open(Buffer{}.append(sess->basePath, "/index").c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_LARGEFILE, 0775);

       if (fd == -1)
                throw Switch::system_error("Failed to persist index");

	// respect 2GB limit
        for (size_t o{0}; o != sess->indexOut.size();)
        {
                const auto upto = std::min<size_t>(o + uint32_t(2 * 1024 * 1024 * 1024) - 1, sess->indexOut.size());
                int res = write(fd, sess->indexOut.data() + o, upto - o);

                if (res == -1)
                {
                        close(fd);
                        throw Switch::system_error("Failed to persist index");
                }

                o = upto;
        }

        if (close(fd) == -1)
                throw Switch::system_error("Failed to persist index");

	// Persist masked documents if any
	pack_updates(updatedDocumentIDs, &maskedProductsBuf);

	if (maskedProductsBuf.size())
	{
		fd = open(Buffer{}.append(sess->basePath, "/masked_documents").c_str(), O_WRONLY|O_CREAT|O_TRUNC|O_LARGEFILE, 0775);

                if (fd == -1)
                        throw Switch::system_error("Failed to persist masked documents");
		else if (write(fd, maskedProductsBuf.data(), maskedProductsBuf.size()) != maskedProductsBuf.size())
		{
			close(fd);
                        throw Switch::system_error("Failed to persist masked documents");
		}
		else if (close(fd) == -1)
                        throw Switch::system_error("Failed to persist masked documents");

        }


	// Persist terms dictionary
        {
                auto kv = dictionary.KeysWithValues();

                std::sort(kv.begin(), kv.end(), [](const auto &a, const auto &b) {
                        return Text::StrnncasecmpISO88597(a.key.data(), a.key.size(), b.key.data(), b.key.size()) < 0;
                });

                // Prefix compression etc
                // store in a session(segment) dictionary
                for (const auto &it : kv)
                {
                        [[maybe_unused]] const auto termID = it.key;
			[[maybe_unused]] const auto term = invDict[termID];	 // actual term
                        // store termOffsetsCnt offsets starting from offsets for this term
                        [[maybe_unused]] const auto *offsets = allOffsets.data() + offsetsMap[termID];
                }
        }

        sess->end();
}
