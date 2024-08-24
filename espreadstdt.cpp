#include "espmanger.h"

void CEsp::_dostdt_op_findparts(STDTrec& oRec, const char* &searchPtr, const char* &endPtr)
{
    size_t taglen = 4;
    oRec.m_pAnam = &BADSTDTANAMREC;
    oRec.m_pBnam = &BADSTDTBNAMREC;
    oRec.m_pDnam = &BADSTDTDNAMREC;
    oRec.m_pEdid = &BADEDIDREC;
    oRec.m_isBad = true;
    oRec.m_isMissingMatchingBfce = false;
    oRec.m_oComp.clear();

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
                    searchPtr += BSKIP(oRec.m_pEdid); // Skip forward
                    break;
                }
            }
        }
        searchPtr++;
    }

    // TODO: build out other STDT data records, avoid searchPtr loop
    while (searchPtr < endPtr)
    {
        if (BLEFT >= taglen)
        {   
            if (memcmp(searchPtr, "BFCB", taglen) == 0)
            {
                if (BLEFT >= sizeof(BFCBrecOv)+sizeof(BFCBDatarecOv))
                {
                    oRec.m_isMissingMatchingBfce = _readBFCBtoBFBE(searchPtr, endPtr, oRec.m_oComp);
                    continue; // If we move the searcPtr ourselves we have to continue to avoid extra ++
                }
            }
            else
            if (memcmp(searchPtr, "ANAM", taglen) == 0)
            {
                if (BLEFT >= sizeof(STDTAnamOv))
                {
                    oRec.m_pAnam = reinterpret_cast<const STDTAnamOv*>(searchPtr);
                    searchPtr += BSKIP(oRec.m_pAnam); // Skip forward
                    continue;
                }
            }
            else
            if (memcmp(searchPtr, "BNAM", taglen) == 0)
            {
                if (BLEFT >= sizeof(STDTBnamOv))
                {
                    oRec.m_pBnam = reinterpret_cast<const STDTBnamOv*>(searchPtr);
                    searchPtr += BSKIP(oRec.m_pBnam); // Skip forward
                    continue;
                }
            }
            else
            if (memcmp(searchPtr, "DNAM", taglen) == 0)
            {
                if (BLEFT >= sizeof(STDTDnamOv))
                {
                    oRec.m_pDnam = reinterpret_cast<const STDTDnamOv*>(searchPtr);
                    searchPtr += BSKIP(oRec.m_pDnam); // Skip forward
                    continue;
                }
                break; // Done, not looking for anything also (assumes DNAM is last in data)
            }
        }
        searchPtr++;
    }


    // Check if record okay
    if (oRec.m_pEdid->m_size && oRec.m_pEdid->m_name && oRec.m_pAnam->m_size && oRec.m_pAnam->m_aname && oRec.m_pBnam->m_size && oRec.m_pDnam->m_size &&
        !isBadPosition(oRec.getfPos()) && oRec.m_oComp.size()<MAXCOMPINREC)
        oRec.m_isBad = false;
}

void CEsp::_dostdt_op(size_t iStdtIdx)
{
    std::string str;
    STDTrec& oRec = m_stdts[iStdtIdx];

    const char* searchPtr = (char*)oRec.m_pHdr + sizeof(STDTHdrOv); // Skip past the header
    const char* endPtr = std::min((const char *)(&m_buffer[m_buffer.size() - 1]), &searchPtr[oRec.m_pHdr->m_size-1]);

    _dostdt_op_findparts(oRec, searchPtr, endPtr);

    if (oRec.m_isMissingMatchingBfce)
    {
        std::lock_guard<std::mutex> guard(m_output_mutex);
        m_MissingBfceMap[oRec.m_pHdr->m_formid] = GENrec(eESP_STDT, iStdtIdx);
    }
    else
    if (oRec.m_isBad)
    {
        // Add to bad list
        std::lock_guard<std::mutex> guard(m_output_mutex);
        m_BadMap[oRec.m_pHdr->m_formid] = GENrec(eESP_STDT, iStdtIdx);
    }
    else
    {
        // Add to starsystem id index now we found last thing interested in
        std::lock_guard<std::mutex> guard(m_output_mutex);
        m_SystemIDMap[oRec.m_pDnam->m_systemId].push_back(GENrec(eESP_STDT, iStdtIdx));
    }
}

void CEsp::process_stdt_ranged_op_mt(size_t start, size_t end)
{
    for (size_t i = start; i < end; ++i)
        _dostdt_op(i);
}

void CEsp::dostdt_op_mt()
{
    size_t num_pointers = m_stdts.size();
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
            futures.push_back(std::async(std::launch::async, &CEsp::process_stdt_ranged_op_mt, this, start, end));
    }

    // Wait for all threads to complete
    for (auto& future : futures)
        future.get();
}
