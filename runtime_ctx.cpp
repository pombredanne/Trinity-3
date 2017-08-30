#include "runtime_ctx.h"

using namespace Trinity;

bool runtime_ctx::termsrun::operator==(const termsrun &o) const noexcept
{
        if (size == o.size)
        {
                for (uint32_t i{0}; i != size; ++i)
                {
                        if (terms[i] != o.terms[i])
                                return false;
                }
                return true;
        }
        else
                return false;
}

bool runtime_ctx::termsrun::is_set(const exec_term_id_t id) const noexcept
{
        for (uint32_t i{0}; i != size; ++i)
        {
                if (terms[i] == id)
                        return true;
        }
        return false;
}

bool runtime_ctx::termsrun::erase(const exec_term_id_t id) noexcept
{
        for (uint32_t i{0}; i != size; ++i)
        {
                if (terms[i] == id)
                {
                        memmove(terms + i, terms + i + 1, (--size - i) * sizeof(terms[0]));
                        return true;
                }
        }

        return false;
}

bool runtime_ctx::termsrun::erase(const termsrun &o)
{
        // Fast scalar intersection scheme designed by N.Kurz.
        // lemire/SIMDCompressionAndIntersection.cpp
        const auto *A = terms, *B = o.terms;
        const auto endA = terms + size;
        const auto endB = o.terms + o.size;
        auto out{terms};

        for (;;)
        {
                while (*A < *B)
                {
                l1:
                        *out++ = *A;
                        if (++A == endA)
                        {
                        l2:
                                if (const auto n = out - terms; n == size)
                                        return false;
                                else
                                {
                                        size = n;
                                        return true;
                                }
                        }
                }

                while (*A > *B)
                {
                        if (++B == endB)
                        {
                                while (A != endA)
                                        *out++ = *A++;
                                goto l2;
                        }
                }

                if (*A == *B)
                {
                        if (++A == endA || ++B == endB)
                        {
                                while (A != endA)
                                        *out++ = *A++;
                                goto l2;
                        }
                }
                else
                        goto l1;
        }
}

uint8_t runtime_ctx::phrase::intersection(const termsrun *const tr, exec_term_id_t *const out) const noexcept
{
        uint16_t n{0};

        for (uint32_t i{0}; i != size; ++i)
        {
                if (const auto id = termIDs[i]; tr->is_set(id))
                        out[n++] = id;
        }
        return n;
}

uint8_t runtime_ctx::phrase::disjoint_union(const termsrun *const tr, exec_term_id_t *const out) const noexcept
{
        uint16_t n{0};
        const auto cnt = tr->size;

        for (uint32_t i{0}; i != cnt; ++i)
        {
                if (const auto id = tr->terms[i]; !is_set(id))
                        out[n++] = id;
        }
        return n;
}

bool runtime_ctx::phrase::intersected_by(const termsrun *const tr) const noexcept
{
        if (tr->size >= size)
        {
                for (uint32_t i{0}; i != size; ++i)
                {
                        if (!tr->is_set(termIDs[i]))
                                return false;
                }
                return true;
        }
        else
                return false;
}

bool runtime_ctx::phrase::operator==(const phrase &o) const noexcept
{
        if (size == o.size)
        {
                for (uint32_t i{0}; i != size; ++i)
                {
                        if (termIDs[i] != o.termIDs[i])
                                return false;
                }
                return true;
        }
        else
                return false;
}

bool runtime_ctx::phrase::is_set(const exec_term_id_t *const l, const uint8_t n) const noexcept
{
        if (n <= size)
        {
                const auto upto = size - n;

                for (uint32_t i{0}; i != upto; ++i)
                {
                        uint32_t k;

                        for (k = 0; k != n && l[k] == termIDs[i + k]; ++k)
                                continue;

                        if (k == n)
                                return true;
                }
        }

        return false;
}

bool runtime_ctx::phrase::is_set(const exec_term_id_t id) const noexcept
{
        for (uint32_t i{0}; i != size; ++i)
        {
                if (termIDs[i] == id)
                        return true;
        }
        return false;
}

runtime_ctx::~runtime_ctx()
{
#ifdef USE_BANKS
	while (banks.size())
	{
		delete banks.back();
		banks.pop_back();
	}

	while (reusableBanks.size())
	{
		delete reusableBanks.back();
		reusableBanks.pop_back();
	}
#endif

        while (allIterators.size())
        {
                delete allIterators.back();
                allIterators.pop_back();
        }

        for (auto ptr : docsetsIterators)
        {
                switch (ptr->type)
                {
                        case DocsSetIterators::Type::Filter:
                                delete static_cast<DocsSetIterators::Filter *>(ptr);
                                break;

                        case DocsSetIterators::Type::Optional:
                                delete static_cast<DocsSetIterators::Optional *>(ptr);
                                break;

                        case DocsSetIterators::Type::OptionalOptPLI:
                                delete static_cast<DocsSetIterators::OptionalOptPLI *>(ptr);
                                break;

                        case DocsSetIterators::Type::OptionalAllPLI:
                                delete static_cast<DocsSetIterators::OptionalAllPLI *>(ptr);
                                break;

                        case DocsSetIterators::Type::Disjunction:
                                delete static_cast<DocsSetIterators::Disjunction *>(ptr);
				break;

                        case DocsSetIterators::Type::DisjunctionAllPLI:
                                delete static_cast<DocsSetIterators::DisjunctionAllPLI *>(ptr);
                                break;


                        case DocsSetIterators::Type::Conjuction:
                                delete static_cast<DocsSetIterators::Conjuction *>(ptr);
				break;

                        case DocsSetIterators::Type::ConjuctionAllPLI:
                                delete static_cast<DocsSetIterators::ConjuctionAllPLI *>(ptr);
                                break;

                        case DocsSetIterators::Type::Phrase:
                                delete static_cast<DocsSetIterators::Phrase *>(ptr);
                                break;

                        case DocsSetIterators::Type::Dummy:
                        case DocsSetIterators::Type::PostingsListIterator:
                                break;
                }
        }

        while (auto p = reusableCDS.pop_one())
                delete p;

        if (reusableCDS.data)
                std::free(reusableCDS.data);
}

void runtime_ctx::prepare_decoder(exec_term_id_t termID)
{
        decode_ctx.check(termID);

        if (!decode_ctx.decoders[termID])
        {
                const auto p = tctxMap[termID];

                decode_ctx.decoders[termID] = idxsrc->new_postings_decoder(termID, p.second, p.first);
        }

        require(decode_ctx.decoders[termID]);
}

exec_term_id_t runtime_ctx::resolve_term(const str8_t term)
{
        exec_term_id_t *ptr;

#ifndef LEAN_SWITCH
        if (termsDict.Add(term, 0, &ptr))
#else
        auto p = termsDict.insert({term, 0});

        ptr = &p.first->second;
        if (p.second)
#endif
        {
                const auto tctx = idxsrc->term_ctx(term);

                if (tctx.documents == 0)
                {
                        // matches no documents, unknown
                        *ptr = 0;
                }
                else
                {
                        *ptr = termsDict.size();
                        tctxMap.insert({*ptr, {tctx, term}});
                }
        }

        return *ptr;
}

runtime_ctx::phrase *runtime_ctx::register_phrase(const Trinity::phrase *p)
{
        auto ptr = (phrase *)allocator.Alloc(sizeof(phrase) + sizeof(exec_term_id_t) * p->size);

        ptr->size = p->size;
        for (uint32_t i{0}; i != p->size; ++i)
        {
                if (const auto id = resolve_term(p->terms[i].token))
                        ptr->termIDs[i] = id;
                else
                        return nullptr;
        }

        return ptr;
}

void runtime_ctx::decode_ctx_struct::check(const uint16_t idx)
{
        if (idx >= capacity)
        {
                const auto newCapacity{idx + 8};

                decoders = (Trinity::Codecs::Decoder **)std::realloc(decoders, sizeof(Trinity::Codecs::Decoder *) * newCapacity);
                memset(decoders + capacity, 0, (newCapacity - capacity) * sizeof(Trinity::Codecs::Decoder *));
                capacity = newCapacity;
        }
}

runtime_ctx::decode_ctx_struct::~decode_ctx_struct()
{
        for (uint32_t i{0}; i != capacity; ++i)
                delete decoders[i];

        if (decoders)
                std::free(decoders);
}

Trinity::term_hits *candidate_document::materialize_term_hits(runtime_ctx *rctx, Codecs::PostingsListIterator *const it, const exec_term_id_t termID)
{
        auto *const __restrict__ th = termHits + termID;
        const auto did = it->curDocument.id;

        if (th->docID != did)
        {
                const auto docHits = it->freq;

                th->docID = did;
                th->set_freq(docHits);

                if (!matchedDocument.dws)
                        matchedDocument.dws = new DocWordsSpace(rctx->idxsrc->max_indexed_position());

                if (!dwsInUse)
                {
                        // deferred until we will need to do this
                        matchedDocument.dws->reset();
                        dwsInUse = true;
                }

                it->materialize_hits(termID, matchedDocument.dws, th->all);
        }

        return th;
}

Trinity::candidate_document::candidate_document(runtime_ctx *const rctx)
{
        const auto maxQueryTermIDPlus1 = rctx->termsDict.size() + 1;

        curDocQueryTokensCaptured = (isrc_docid_t *)calloc(sizeof(isrc_docid_t), maxQueryTermIDPlus1);
        termHits = new term_hits[maxQueryTermIDPlus1];
        matchedDocument.matchedTerms = (matched_query_term *)rctx->allocator.Alloc(sizeof(matched_query_term) * maxQueryTermIDPlus1);
}

void runtime_ctx::_reusable_cds::push_back(candidate_document *const d)
{
        if (unlikely(size_ == capacity))
        {
                // can't hold no more
                delete d;
        }
        else
        {
                data[size_++] = d;
        }
}

static void collect_doc_matching_terms(Trinity::DocsSetIterators::Iterator *const it,
                                       const isrc_docid_t docID,
                                       Trinity::iterators_collector *const out)
{
        switch (it->type)
        {
                case DocsSetIterators::Type::Phrase:
                {
                        const auto I = static_cast<DocsSetIterators::Phrase *>(it);
                        const auto n = I->size;
                        auto its = I->its;

			memcpy(out->data + out->cnt, its, sizeof(its[0]) * n);
			out->cnt += n;
                }
                break;

                case DocsSetIterators::Type::PostingsListIterator:
			out->data[out->cnt++] = reinterpret_cast<Codecs::PostingsListIterator *>(it);
                        break;

		case DocsSetIterators::Type::Optional:
			collect_doc_matching_terms(reinterpret_cast<DocsSetIterators::Optional *>(it)->main, docID, out);
			break;

		case DocsSetIterators::Type::OptionalOptPLI:
			collect_doc_matching_terms(reinterpret_cast<DocsSetIterators::OptionalOptPLI *>(it)->main, docID, out);
			break;

		case DocsSetIterators::Type::OptionalAllPLI:
			out->data[out->cnt++] = reinterpret_cast<DocsSetIterators::OptionalAllPLI *>(it)->main;
			break;

                case DocsSetIterators::Type::Filter:
                        collect_doc_matching_terms(reinterpret_cast<DocsSetIterators::Filter *>(it)->req, docID, out);
                        break;

                case DocsSetIterators::Type::Conjuction:
                {
                        const auto I = static_cast<DocsSetIterators::Conjuction *>(it);
                        const auto n = I->size;
                        auto its = I->its;

                        for (uint16_t i{0}; i != n; ++i)
                                collect_doc_matching_terms(its[i], docID, out);
                }
                break;

		case DocsSetIterators::Type::ConjuctionAllPLI:
                {
                        const auto I = static_cast<const DocsSetIterators::ConjuctionAllPLI *>(it);
                        const auto n = I->size;
                        auto its = I->its;

			memcpy(out->data + out->cnt, its, sizeof(its[0]) * n);
			out->cnt += n;
                }
                break;

                case DocsSetIterators::Type::DisjunctionAllPLI:
                {
                        const auto I = static_cast<DocsSetIterators::Disjunction *>(it);
                        const auto &pq = I->pq;
                        const auto size = pq.size();
                        const auto heap = pq.data();

			out->data[out->cnt++] = (Codecs::PostingsListIterator *)heap[0];
                        if (size >= 3)
                        {
                                auto &stack = I->istack;

                                stack.data[0] = 1;
                                stack.data[1] = 2;
                                stack.cnt = 2;

                                do
                                {
                                        const auto i = stack.data[--stack.cnt];

                                        if (auto it = heap[i]; it->current() == docID)
                                        {
						out->data[out->cnt++] = (Codecs::PostingsListIterator *)it;

                                                const auto left = ((i + 1) << 1) - 1;
                                                const auto right = left + 1;

                                                if (right < size)
                                                {
                                                        stack.data[stack.cnt++] = left;
                                                        stack.data[stack.cnt++] = right;
                                                }
                                                else if (left < size && (it = heap[left])->current() == docID)
                                                {
							out->data[out->cnt++] = (Codecs::PostingsListIterator *)it;
                                                }
                                        }
                                } while (stack.cnt);
                        }
                        else if (size == 2 && heap[1]->current() == docID)
                        {
                                out->data[out->cnt++] = (Codecs::PostingsListIterator *)heap[1];
                        }
                }
                break;

                case DocsSetIterators::Type::Disjunction:
                {
                        const auto I = static_cast<DocsSetIterators::Disjunction *>(it);
                        const auto &pq = I->pq;
                        const auto size = pq.size();
                        const auto heap = pq.data();

                        collect_doc_matching_terms(heap[0], docID, out);
                        if (size >= 3)
                        {
                                auto &stack = I->istack;

                                stack.data[0] = 1;
                                stack.data[1] = 2;
                                stack.cnt = 2;

                                do
                                {
                                        const auto i = stack.data[--stack.cnt];

                                        if (auto it = heap[i]; it->current() == docID)
                                        {
                                                collect_doc_matching_terms(it, docID, out);

                                                const auto left = ((i + 1) << 1) - 1;
                                                const auto right = left + 1;

                                                if (right < size)
                                                {
                                                        stack.data[stack.cnt++] = left;
                                                        stack.data[stack.cnt++] = right;
                                                }
                                                else if (left < size && (it = heap[left])->current() == docID)
                                                {
                                                        collect_doc_matching_terms(it, docID, out);
                                                }
                                        }
                                } while (stack.cnt);
                        }
                        else if (size == 2 && heap[1]->current() == docID)
                        {
                                collect_doc_matching_terms(heap[1], docID, out);
                        }
                }
                break;

                default:
                        SLog("IMPLEMENT ME\n");
                        exit(1);
        }
}

void Trinity::runtime_ctx::prepare_match(Trinity::candidate_document *const doc)
{
        auto &md = doc->matchedDocument;
        const auto did = doc->id;
	auto dws = md.dws;

        collectedIts.cnt = 0;
        collect_doc_matching_terms(rootIterator, doc->id, &collectedIts);
	md.matchedTermsCnt = 0;

	//SLog("collectedIts.size() = ", collectedIts.size(), "\n");


	if (!dws)
		dws = md.dws = new DocWordsSpace(idxsrc->max_indexed_position());

	if (!doc->dwsInUse)
		dws->reset();
	else
		doc->dwsInUse = false;


	const auto cnt = collectedIts.cnt;
	auto *const data = collectedIts.data;

	for (uint32_t i{0}; i != cnt; ++i)
	{
		auto *const it = data[i];
                const auto tid = it->decoder()->exec_ctx_termid();

                if (const auto *const qti = originalQueryTermCtx[tid]) // not in a NOT branch
                {
                        if (doc->curDocQueryTokensCaptured[tid] != doc->curDocSeq)
                        {
                                auto *const p = md.matchedTerms + md.matchedTermsCnt++;
                                auto *const th = doc->termHits + tid;

                                doc->curDocQueryTokensCaptured[tid] = doc->curDocSeq;
                                p->queryCtx = qti;
                                p->hits = th;

                                if (th->docID != did)
                                {
					// could have been materialized earlier for a phrase check
                                        const auto docHits = it->freq;

                                        th->docID = did;
                                        th->set_freq(docHits);
                                        it->materialize_hits(tid, dws, th->all);
                                }
                        }
                }
        }
}

void Trinity::runtime_ctx::forget_document(candidate_document *const doc)
{
#ifdef USE_BANKS
	forget_document_inbank(doc);
#endif
}

static constexpr bool traceBindings{false};

Trinity::candidate_document *Trinity::runtime_ctx::lookup_document(const isrc_docid_t id)
{
#ifdef USE_BANKS
	return lookup_document_inbank(id);
#else
        auto &v = trackedDocuments[id & (sizeof_array(trackedDocuments) - 1)];

        if (traceBindings)
                SLog("Lookup among ", v.size(), "\n");

	auto data = v.data();
	auto size = v.size();

        for (uint32_t i{0}; i < size; )
        {
                auto doc = data[i];

                if (!doc->bindCnt)
                {
                        if (traceBindings)
                                SLog("Letting go of document\n");

                        cds_release(doc);
			data[i] = data[--size];
                        v.pop_back();
                }
                else if (doc->id == id)
                {
                        if (traceBindings)
                                SLog("Found at ", i, "\n");

                        return doc;
                }
                else
                        ++i;
        }
        return nullptr;
#endif
}

void Trinity::runtime_ctx::unbind_document(candidate_document *&dt)
{
        // you are expected to have checked if (p->lastMaterializedDoc) before invoking this method
        if (traceBindings)
                SLog("unbinding ", dt->bindCnt, "\n");

        auto d = dt;

        if (1 == d->bindCnt--)
	{
                forget_document(d);
#ifdef USE_BANKS
		cds_release(d);
#endif
	}
        else // Will defer release to lookup_document(). Will only release if more bound to that document
        {
                cds_release(d);
        }

	dt = nullptr;
}

void Trinity::runtime_ctx::bind_document(candidate_document *&dt, candidate_document *const doc)
{
        if (auto prev = dt)
        {
		if (unlikely(prev == doc))
		{
			// binding to the same document
			// can't forget_document() and track_document()
			// because if prev->bindCnt == 1 now, it will forget i.e release
			// and then will attempt to track it once it's been released
			return;
		}

                if (1 == prev->bindCnt--)
                {
                        forget_document(prev);
#ifdef USE_BANKS
                        cds_release(prev);
#endif
                }
                else // Will defer release to lookup_document(). Will only release if more bound to that document
                {
                        cds_release(prev);
                }
        }


        if (0 == doc->bindCnt++)
        {
                track_document(doc);
        }

        dt = doc->retained();
}

#ifdef USE_BANKS
Trinity::docstracker_bank *Trinity::runtime_ctx::new_bank(const Trinity::isrc_docid_t base)
{
        if (reusableBanks.size())
        {
                auto b = reusableBanks.back();

                reusableBanks.pop_back();
#ifdef BANKS_USE_BM
                memset(b->bm, 0, docstracker_bank::BM_SIZE * sizeof(uint64_t));
#else
                memset(b->entries, 0, sizeof(docstracker_bank::entry) * docstracker_bank::SIZE);
#endif

                b->base = base;
                b->setCnt = 0;

		lastBank = b;
                banks.push_back(b);
                return b;
        }

        auto b = new docstracker_bank();

#ifdef BANKS_USE_BM
        memset(b->bm, 0, docstracker_bank::BM_SIZE * sizeof(uint64_t));
#else
        memset(b->entries, 0, sizeof(docstracker_bank::entry) * docstracker_bank::SIZE);
#endif
        b->base = base;
        b->setCnt = 0;

        lastBank = b;
        banks.push_back(b);

        return b;
}

void Trinity::runtime_ctx::forget_document_inbank(Trinity::candidate_document *const doc)
{
	const auto id = doc->id;
	auto b = bank_for(id);

	if (1 == b->setCnt--)
	{
		if (lastBank == b)
			lastBank = nullptr;

		for (uint32_t i{0}; i != banks.size(); ++i)
		{
			if (banks[i] == b)
			{
				banks[i] = banks.back();
				banks.pop_back();
				break;
			}
		}

		reusableBanks.push_back(b);
		lastBank = nullptr;
	}
	else
	{
		const auto idx = id - b->base;

#ifdef BANKS_USE_BM
                b->bm[idx >> 6] &= ~(uint64_t(1) << (idx & (docstracker_bank::SIZE - 1)));
#else
                b->entries[idx].document = nullptr;
#endif
	}
}

Trinity::candidate_document *Trinity::runtime_ctx::lookup_document_inbank(const isrc_docid_t id)
{
        if (const auto b = bank_for(id))
        {
                const auto idx = id - b->base;

#ifdef BANKS_USE_BM
                if (b->bm[idx >> 6] & (uint64_t(1) << (idx & (docstracker_bank::SIZE - 1))))
#endif
                        return b->entries[idx].document;
        }

        return nullptr;
}

void Trinity::runtime_ctx::track_document_inbank(Trinity::candidate_document *const d)
{
	const auto id = d->id;
	auto b = bank_for(id);
	const auto idx= id - b->base;

#ifdef BANKS_USE_BM
	b->bm[idx >> 6] |= uint64_t(1) << (idx & (docstracker_bank::SIZE - 1));
#endif
	b->entries[idx].document = d;
	++(b->setCnt);
}
#endif