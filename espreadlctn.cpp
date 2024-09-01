#include "espmanger.h"

// Read location records LCTN
void CEsp::_dolctn_op_findparts(LCTNrec& oRec, const char* &searchPtr, const char* &endPtr)
{
    size_t taglen = 4;
    oRec.m_pEdid = &BADEDIDREC;
    oRec.m_pFull = &BADFULLREC;
    oRec.m_pKsiz = &BADKSIZREC;
    oRec.m_pData = &BADLCTNDATAREC;
    oRec.m_pAnam = &BADLCTNANAMREC;
    oRec.m_pPnam = &BADLCTNPNAMREC;
    oRec.m_pXnam = &BADLCTNXNAMREC;
    oRec.m_isBad = true;

    // Find the EDID
    while (searchPtr < endPtr)
    {
        if (BLEFT >= taglen)
        { 
            if (memcmp(searchPtr, "EDID", taglen) == 0) 
            {
                if (BLEFT >= sizeof(EDIDrecOv))
                {
                    oRec.m_pEdid = reinterpret_cast<const EDIDrecOv*>(searchPtr);
                    searchPtr +=  BSKIP(oRec.m_pEdid); // Skip forward
                    break;
                }
            }
        }
        searchPtr++;
    }

    while (searchPtr < endPtr)
    {
        if (BLEFT >= taglen)
        { 
            if (memcmp(searchPtr, "FULL", taglen) == 0) 
            {
                if (BLEFT >= sizeof(FULLrecOv))
                {
                    oRec.m_pFull = reinterpret_cast<const FULLrecOv*>(searchPtr);
                    searchPtr += BSKIP(oRec.m_pFull); // Skip forward
                    break;
                }
            }
        }
        searchPtr++;
    }

    // Get the keywords
    while (searchPtr < endPtr)
    {
        if (BLEFT >= taglen)
        { 
            if (memcmp(searchPtr, "KSIZ", taglen) == 0) 
            {
                if (BLEFT >= sizeof(KSIZrecOv))
                {
                    oRec.m_pKsiz = reinterpret_cast<const KSIZrecOv*>(searchPtr);
                    searchPtr += BSKIP(oRec.m_pKsiz); // Skip forward

                    if (BLEFT >= sizeof(KWDArecOv))
                    {
                        oRec.m_pKwda = reinterpret_cast<const KWDArecOv*>(searchPtr);
                        searchPtr += BSKIP(oRec.m_pKwda); // Skip forward
                    }
                    break; 
                }
            }
        }
        searchPtr++;
    }

    // Find the data section
    while (searchPtr < endPtr)
    {
        if (BLEFT >= taglen)
        {   
            if (memcmp(searchPtr, "DATA", taglen) == 0)
            {
                if (BLEFT >= sizeof(LCTNDataOv))
                {
                    oRec.m_pData = reinterpret_cast<const LCTNDataOv*>(searchPtr);
                    searchPtr += BSKIP(oRec.m_pData); // Skip forward
                    break;
                }
            }
        }
        searchPtr++;
    }

    // get the ANAM near the end
    while (searchPtr < endPtr)
    {
        if (BLEFT >= taglen)
        {   
            if (memcmp(searchPtr, "ANAM", taglen) == 0)
            {
                if (BLEFT >= sizeof(LCTNAnamOv))
                {
                    oRec.m_pAnam = reinterpret_cast<const LCTNAnamOv*>(searchPtr);
                    searchPtr += BSKIP(oRec.m_pAnam); // Skip forward
                    continue;
                }
            }
            else
            if (memcmp(searchPtr, "PNAM", taglen) == 0)
            {
                if (BLEFT >= sizeof(LCTNPnamOv))
                {
                    oRec.m_pPnam = reinterpret_cast<const LCTNPnamOv*>(searchPtr);
                    searchPtr += BSKIP(oRec.m_pPnam); // Skip forward
                    continue;
                }
            }
            else
            if (memcmp(searchPtr, "XNAM", taglen) == 0)
            {
                if (BLEFT >= sizeof(LCTNXnamOv))
                {
                    oRec.m_pXnam = reinterpret_cast<const LCTNXnamOv*>(searchPtr);
                    searchPtr += BSKIP(oRec.m_pXnam); // Skip forward
                    break; // DONE
                }
            }
        }
        searchPtr++;
    }

    // Check if record okay
    if (oRec.m_pEdid->m_size && oRec.m_pEdid->m_name)
    {   // oRec.m_pAnam->m_size && oRec.m_pAnam->m_aname don't check these many LCTN have this empty

        // For ESM files some records don't have inline strings but references, so don't check Full records as if they are a string
        if (isESM())
            oRec.m_isBad = false;
        else
        if (oRec.m_pFull->m_size && oRec.m_pFull->m_name)
            oRec.m_isBad = false;
    }
}

void CEsp::_dolctn_op(size_t iIdx)
{
    std::string str;
    LCTNrec& oRec = m_lctns[iIdx];

    const char* searchPtr = (char*)oRec.m_pHdr + sizeof(LCTNHdrOv); // Skip past the header
    const char* endPtr = std::min((const char *)(&m_buffer[m_buffer.size() - 1]), &searchPtr[oRec.m_pHdr->m_size-1]);

    _dolctn_op_findparts(oRec, searchPtr, endPtr);

    if (oRec.m_isBad)
    {
        // Add to bad list
        std::lock_guard<std::mutex> guard(m_output_mutex);
        m_BadMap[oRec.m_pHdr->m_formid] = GENrec(eESP_LCTN, iIdx);
    }
    else
    if (oRec.m_pXnam->m_size) // Don't add to map if we did not fins an XNAM record as we have no key
    {
        // Add to star system id index for the location
        std::lock_guard<std::mutex> guard(m_output_mutex);
        m_SystemIDMap[oRec.m_pXnam->m_systemId].push_back(GENrec(eESP_LCTN, iIdx));
    }
}

void CEsp::process_lctn_ranged_op_mt(size_t start, size_t end)
{
    for (size_t i = start; i < end; ++i)
        _dolctn_op(i);
}

// TODO Make this general function that takes function as a param
void CEsp::dolctn_op_mt()
{
    size_t num_pointers = m_lctns.size();
    if (num_pointers == 0) return; // No work to do if no pointers
    size_t num_threads = 0;
    size_t chunk_size = 0;

    chunk_size = getmtclunks(num_pointers, num_threads);
    std::vector<std::future<void>> futures;

    for (size_t i = 0; i < num_threads; ++i)
    {
        size_t start = i * chunk_size;
        size_t end = std::min(start + chunk_size, num_pointers);

        // Ensure start < end
        if (start < end)
            futures.push_back(std::async(std::launch::async, &CEsp::process_lctn_ranged_op_mt, this, start, end));
    }

    // Wait for all threads to complete
    for (auto& future : futures)
        future.get();
}

void CEsp::do_process_lctns()
{
    // process locations to build location structures for stars/planets
    // universe -> star -> surface/orbital/poi

    // TODO if needed
}
