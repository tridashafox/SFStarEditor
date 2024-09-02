#include "espmanger.h"

const char* SZBADRECORD = "";

// start of read process, reads the header details and kicks off the other reads
// Built up records with header information before next stage
void CEsp::_dobuildsubrecs_mt(const std::vector<const CEsp::GRUPHdrOv *>& vgrps, const char* searchPatt, CEsp::ESPRECTYPE eType)
{
    std::unordered_map<formid_t, GENrec> oLocalFmIdMap;
    std::vector<STDTrec> oLocalstdts;
    std::vector<PNDTrec> oLocalpndts;
    std::vector<LCTNrec> oLocallctns;

    const size_t pattLen = 4;
    const char* ptrofbuffer = m_buffer.data() + m_buffer.size();

    for (const GRUPHdrOv * pGrupHdr : vgrps)
    {
        const char * grupPtr = reinterpret_cast<const char*>(pGrupHdr);
        if (grupPtr < ptrofbuffer)
        {
            const char* searchPtr = grupPtr + sizeof(GRUPHdrOv);
            const char* endPtr = grupPtr + pGrupHdr->m_size;

            if (endPtr > ptrofbuffer) // group end pointer is beyond file data - bad data
                break;

            while (searchPtr < endPtr)
            {
                if (BLEFT < pattLen) // not enough data in group to check what record is in the group
                    break;

                if (memcmp(searchPtr, searchPatt, pattLen) == 0)
                {
                    if (eType == eESP_STDT)
                    {
                        // check the record is big enough to overlay
                        if (BLEFT >= sizeof(STDTHdrOv))
                        {
                            STDTrec oRec{};
                            oRec.m_pHdr = reinterpret_cast<const STDTHdrOv*>(searchPtr);
                            if (oRec.m_pHdr->m_size) // Skip empty records
                            {
                                oLocalstdts.push_back(oRec);
                                oLocalFmIdMap[oRec.m_pHdr->m_formid] = GENrec(eType, m_stdts.size() - 1); // Add record to form id map
                            }
                            searchPtr += BSKIP(oRec.m_pHdr);
                        }
                    }
                    else
                    if (eType == eESP_PNDT)
                    {
                        if (BLEFT >= sizeof(PNDTHdrOv))
                        {
                            PNDTrec oRec{};
                            oRec.m_pHdr = reinterpret_cast<const PNDTHdrOv*>(searchPtr);
                            if (oRec.m_pHdr->m_size)
                            {
                                oRec.m_pcompdata = &searchPtr[sizeof(PNDTHdrOv)];
                                oRec.m_compdatasize = oRec.m_pHdr->m_size - sizeof(oRec.m_pHdr->m_decompsize);
                                oLocalpndts.push_back(oRec);
                                oLocalFmIdMap[oRec.m_pHdr->m_formid] = GENrec(eType, m_pndts.size() - 1); // Add record to form id map
                            }
                            searchPtr += BSKIP(oRec.m_pHdr);
                        }
                    }
                    else
                    if (eType == eESP_LCTN)
                    {
                        if (BLEFT >= sizeof(LCTNHdrOv))
                        {
                            LCTNrec oRec{};
                            oRec.m_pHdr = reinterpret_cast<const LCTNHdrOv*>(searchPtr);
                            if (oRec.m_pHdr->m_size)
                            {
                                oLocallctns.push_back(oRec);
                                oLocalFmIdMap[oRec.m_pHdr->m_formid] = GENrec(eType, m_lctns.size() - 1); // Add record to form id map
                            }
                            searchPtr += BSKIP(oRec.m_pHdr);

                        }
                    }
                    else
                    {
                        // TODO: others
                    }
                }
                searchPtr++;
            }
        }
    }

    // do form map build from local data to global at end to allow multi-thread to work effectively
    if (!oLocalstdts.empty() || !oLocalpndts.empty() || !oLocallctns.empty())
    {   
        std::lock_guard<std::mutex> guard(m_output_mutex);

        if (eType == eESP_STDT) for (auto& oSTDTRec : oLocalstdts)
        {
            m_stdts.push_back(oSTDTRec);
            m_FmIDMap[oSTDTRec.m_pHdr->m_formid] = GENrec(eType, m_stdts.size() - 1); 
        }
        if (eType == eESP_PNDT) for (auto& oPNDTRec : oLocalpndts)
        {
            m_pndts.push_back(oPNDTRec);
            m_FmIDMap[oPNDTRec.m_pHdr->m_formid] = GENrec(eType, m_pndts.size() - 1); 
        }
        if (eType == eESP_LCTN) for (auto& oLCTNRec : oLocallctns)
        {
            m_lctns.push_back(oLCTNRec);
            m_FmIDMap[oLCTNRec.m_pHdr->m_formid] = GENrec(eType, m_lctns.size() - 1);
        }
    }
}

void CEsp::do_process_subrecs_mt()
{
    std::vector<std::future<void>> futures;
    futures.push_back(std::async(std::launch::async, &CEsp::_dobuildsubrecs_mt, this, m_grupStdtPtrs, "STDT", eESP_STDT));
    futures.push_back(std::async(std::launch::async, &CEsp::_dobuildsubrecs_mt, this, m_grupPndtPtrs, "PNDT", eESP_PNDT));
    futures.push_back(std::async(std::launch::async, &CEsp::_dobuildsubrecs_mt, this, m_grupLctnPtrs, "LCTN", eESP_LCTN));

    // Wait for all threads to complete
    for (auto& future : futures)
        future.get();
}

// Work out chunking based on number of HW threads possible for multi-treading
size_t CEsp::getmtclunks(size_t num_pointers, size_t& num_threads)
{
    size_t num_hwthreads = std::thread::hardware_concurrency();
    num_threads = std::min(num_hwthreads, num_pointers);
    return (num_pointers + num_threads - 1) / num_threads; // Ceiling division
}

bool CEsp::loadfile(std::string &strErr)
{
    m_buffer.clear();

    // Open the file for reading in binary mode
    std::ifstream file(m_wstrfilename, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        strErr = std::string("Could not open file ") + getFnameAsStr();
        return false;
    }

    // Get file size
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    m_buffer.resize(size);

    // Read the entire file into the buffer
    if (!file.read(m_buffer.data(), size))
    {
        file.close();
        strErr = std::string("Unable to read data from ") + getFnameAsStr();
        return false;
    }

    // Close the file
    file.close();
    return true; 
}

bool CEsp::loadhdrs()
{
    if (!m_buffer.size())
        return false;

    m_grupStdtPtrs.clear();
    m_grupPndtPtrs.clear();
    m_grupLctnPtrs.clear();
    m_strMasterFile.clear();
    m_stdts.clear();
    m_pndts.clear(); 
    m_lctns.clear(); 

    const char* searchPatternMAST = "MAST";
    const char* searchPatternGRUP = "GRUP";
    const char* searchPatternSTDT = "STDT";
    const char* searchPatternPNDT = "PNDT";
    const char* searchPatternLCTN = "LCTN";
    const size_t taglen = 4;

    const char* searchPtr = m_buffer.data();
    const char* endPtr = m_buffer.data() + m_buffer.size() - 1;

    // TODO: mt this/optmise
    while (searchPtr < endPtr)
    {
        if (memcmp(searchPtr, "GRUP", taglen) == 0)
        {
            if (BLEFT >= sizeof(GRUPHdrOv))
            {
                const GRUPHdrOv *pGrupHdr = reinterpret_cast<const GRUPHdrOv*>(searchPtr);
                const char *startRecsPtr = searchPtr + sizeof(GRUPHdrOv);
                searchPtr += pGrupHdr->m_size;

                if (BLEFT >= taglen)
                {
                    if (memcmp(startRecsPtr, "STDT", taglen) == 0)
                        m_grupStdtPtrs.push_back(pGrupHdr);
                    else
                    if (memcmp(startRecsPtr, "PNDT", taglen) == 0)
                        m_grupPndtPtrs.push_back(pGrupHdr);
                    else
                    if (memcmp(startRecsPtr, "LCTN", taglen) == 0)
                        m_grupLctnPtrs.push_back(pGrupHdr);
                }
            }

        }
        else
        if (memcmp(searchPtr, "MAST", taglen) == 0)
        {
            if (BLEFT >= sizeof(MASTOv))
            {
                const MASTOv* pMast = reinterpret_cast<const MASTOv*>(searchPtr);

                m_strMasterFile = std::string((char*)&pMast->m_name);
                searchPtr += taglen; // TODO: should really have a MASTRecOv
                BSKIP(pMast);
            }
        }
        else
            searchPtr++;
    }

    return true;
}
    
bool CEsp::_loadfrombuffer(std::string &strErr)
{
    m_SystemIDMap.clear();
    m_FmIDMap.clear();

    if (!loadhdrs())
    {
        strErr = "Failed to build header structures.";
        return false;
    }

    // Build the hdrs first
    do_process_subrecs_mt();

    // Build out records beyond just the header information using multiple threads
    dostdt_op_mt();
    dopndt_op_mt();
    dolctn_op_mt();
    do_process_lctns();

    return true;
}


// Load the ESP or ESM file
bool CEsp::load(const std::wstring &strFileName, std::string &strErr)
{
    m_wstrfilename = strFileName;

    #ifdef _DEBUG
    timept ots = startTC();
    #endif

    if (!loadfile(strErr))
        return false;

    #ifdef _DEBUG
    endTC("loadfile", ots);
    #endif

    if (!_loadfrombuffer(strErr))
        return false;

    return true;
}
