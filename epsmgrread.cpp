#include "espmanger.h"

const char* SZBADRECORD = "";

// Function to compress the data and store it in the output parameter
// TODO - not tested
bool CEsp::compress_data(const char* input_data, size_t input_size, std::vector<char>& compressed_data) 
{
    // Initialize the compressor object
    z_stream deflateStream;
    deflateStream.zalloc = Z_NULL;
    deflateStream.zfree = Z_NULL;
    deflateStream.opaque = Z_NULL;
    deflateStream.avail_in = static_cast<uInt>(input_size); // Cast input_size to uInt
    deflateStream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input_data));

    int ret = deflateInit(&deflateStream, Z_BEST_COMPRESSION);
    if (ret != Z_OK) {
        std::cerr << "Failed to initialize compression stream." << std::endl;
        return false;
    }

    // Reserve initial space for compressed data
    uLong combound = static_cast<uLong>(input_size);
    compressed_data.resize(compressBound(combound)); // Estimate the maximum possible compressed size

    deflateStream.next_out = reinterpret_cast<Bytef*>(&compressed_data[0]);
    deflateStream.avail_out = static_cast<uInt>(compressed_data.size());

    // Compress the data
    ret = deflate(&deflateStream, Z_FINISH);
    if (ret != Z_STREAM_END) {
        std::cerr << "Failed to compress data." << std::endl;
        deflateEnd(&deflateStream);
        return false;
    }

    // Finalize compression
    deflateEnd(&deflateStream);

    // Resize the buffer to the actual compressed size
    compressed_data.resize(deflateStream.total_out);

    return true;
}

// Function to decompress the data and store it in the output parameter
bool CEsp::decompress_data(const char* compressed_data, size_t compressed_size, std::vector<char>& decompressed_data, size_t decompressed_size) 
{
    // Initialize the decompressor object
    z_stream inflateStream;
    inflateStream.zalloc = Z_NULL;
    inflateStream.zfree = Z_NULL;
    inflateStream.opaque = Z_NULL;
    inflateStream.avail_in = static_cast<uInt>(compressed_size);
    inflateStream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed_data));

    int ret = inflateInit(&inflateStream);
    if (ret != Z_OK) {
        std::cerr << "Failed to initialize decompression stream." << std::endl;
        return false;
    }

    // Reserve space for the decompressed data based on the provided decompressed_size
    size_t compressed_size__justincase_overflow_buffer = decompressed_size * 3;
    decompressed_data.resize(decompressed_size + compressed_size__justincase_overflow_buffer);

    inflateStream.next_out = reinterpret_cast<Bytef*>(&decompressed_data[0]);
    inflateStream.avail_out = static_cast<uInt>(decompressed_size);

    // Decompress the data
    ret = inflate(&inflateStream, Z_FINISH);
    if (ret != Z_STREAM_END) {
        std::cerr << "Failed to decompress data." << std::endl;
        inflateEnd(&inflateStream);
        return false;
    }

    // Finalize decompression
    inflateEnd(&inflateStream);

    // Resize the vector to the actual decompressed size (in case it's different)
    decompressed_data.resize(decompressed_size - inflateStream.avail_out);

    return true;
}

// TODO: Really should have created a base class for the record types and avoided this kind of checking type thing
CEsp::ESPRECTYPE CEsp::getRecType(const char* ptag)
{
    size_t pattLen;
    if (!ptag || (pattLen = strlen(ptag)) != 4)
        return eESP_IDK;

    pattLen = strlen(ptag);
    if (memcmp(ptag, "PNDT", pattLen) == 0)
        return eESP_PNDT;
    if (memcmp(ptag, "STDT", pattLen) == 0)
        return eESP_STDT;
    return eESP_IDK;
}

// Use index to find a record based on form_id
bool CEsp::findFmIDMap(formid_t formid, GENrec& fndrec)
{
    bool bFnd = false;
    if (bFnd = m_FmIDMap.find(formid) != m_FmIDMap.end())
        fndrec = m_FmIDMap[formid];

    return bFnd;
}

void CEsp::mergeFmIDMaps(std::unordered_map<formid_t, GENrec>& targetMap, const std::unordered_map<formid_t, GENrec>& sourceMap) 
{
    for (const auto& [key, value] : sourceMap) 
        targetMap[key] = value;  // Insert or update the key-value pair
}

    
bool CEsp::findKeyword(const LCTNrec &oRec , uint32_t iKeyword)
{
    for (size_t i = 0; i < oRec.m_pKsiz->m_count; ++i)
    {
        const uint32_t* pdata = (uint32_t*)&oRec.m_pKwda->m_data;
        if (pdata[i] == iKeyword)
            return true;
    }
    return false;
}

// Given a STDT rec goes and finds the locations that go with it
// So it can work out the player level and returns this
bool CEsp::findPlayerLvl(const STDTrec &oRecStar, size_t &iPlayerLvl, size_t &iPlayerLvlMax)
{
    iPlayerLvl = 0;
    iPlayerLvlMax = 255;

    auto it = m_SystemIDMap.find(oRecStar.m_pDnam->m_systemId);
    if (it != m_SystemIDMap.end())
    {
        std::vector<GENrec>& genRecords = it->second;
        for (const GENrec& oGen : genRecords)
            if (oGen.m_eType == eESP_LCTN &&m_lctns[oGen.m_iIdx].m_pData->m_size)
            {
                if (findKeyword(m_lctns[oGen.m_iIdx], KW_LocTypeStarSystem))
                {
                    iPlayerLvl = m_lctns[oGen.m_iIdx].m_pData->m_playerLvl;
                    iPlayerLvlMax = m_lctns[oGen.m_iIdx].m_pData->m_playerLvlMax;
                    return true;
                }
            }
    }

    return false;
}

// Given index to a planet, finds what is orbiting
// TODO: make it work for moons
size_t CEsp::findPrimaryIdx(size_t iIdx, fPos& oSystemPosition)
{
    oSystemPosition.clear();
    if (iIdx >= 0 && iIdx < m_pndts.size())
    {
        formid_t iSystemId = m_pndts[iIdx].m_pGnam->m_systemId;
        auto it = m_SystemIDMap.find(iSystemId);
        if (it != m_SystemIDMap.end())
        {
            std::vector<GENrec>& genRecords = it->second;
            for (const GENrec& oGen : genRecords)
                if (oGen.m_eType == eESP_STDT)
                {
                    oSystemPosition = m_stdts[oGen.m_iIdx].getfPos();
                    return oGen.m_iIdx;
                }
        }
    }
    return NO_ORBIT;
}

    // Given index to a star, find sthe orbiting planets
    // returning these in a vector of idxs of the planets and returns the number found
size_t CEsp::findPndtsFromStdt(size_t iIdx, std::vector<size_t> &oFndPndts)
{
    oFndPndts.clear();

    if (iIdx >= 0 && iIdx < m_stdts.size())
    {
        formid_t iSystemId = m_stdts[iIdx].m_pDnam->m_systemId;

        auto it = m_SystemIDMap.find(iSystemId);
        if (it != m_SystemIDMap.end())
        {
            std::vector<GENrec>& genRecords = it->second;
            for (const GENrec& oRec : genRecords)
                if (oRec.m_eType == eESP_PNDT)
                    oFndPndts.push_back(oRec.m_iIdx);
        }
    }
    return oFndPndts.size();
}


// Check if a STDT position is out of normal bounds
bool CEsp::isBadPosition(const CEsp::fPos& oPos)
{
    // Ignore ones that look invalid if they are too far out of the star map they will mess up
    // the starmap and how it is show
    float iBoundary = STARMAPMAX;
    if (oPos.m_xPos > iBoundary || oPos.m_xPos < -iBoundary ||
        oPos.m_yPos > iBoundary || oPos.m_yPos < -iBoundary ||
        oPos.m_zPos > iBoundary || oPos.m_zPos < -iBoundary)
        return true;

    return false;
}

// for performance - skip past tags in component block we are not intersted in at the moment or don't have overlays for
void CEsp::_doBfcbquickskip(const char * &searchPtr, const char *&endPtr)
{
    std::vector<const char*> otags2b = { "MODL", "FLLD", "XMPM", "XXXX", "PCCC", "RELF", "KWDA", "KSIZ"};
    std::vector<const char*> otags4b = { "BETH", "STRT", "CLAS", "TYPE", "OBJT", "LIST" }; 
    size_t taglen = 4;

    while (searchPtr < endPtr)
    {
        bool bFnd = false;

        for (const auto& tag : otags2b)
            if (memcmp(searchPtr, tag, taglen) == 0)
            {
                searchPtr += taglen;
                searchPtr += sizeof(uint16_t) + *reinterpret_cast<const uint16_t*>(searchPtr);
                bFnd = true;
            }

        for (const auto& tag : otags4b)
            if (memcmp(searchPtr, tag, taglen) == 0)
            {
                searchPtr += taglen;
                searchPtr += sizeof(uint32_t) + *reinterpret_cast<const uint32_t*>(searchPtr);
                bFnd = true;
            }

        if (memcmp(searchPtr, "BFCE", taglen) == 0) 
            break; // found end of component block - can leave now

        if (memcmp(searchPtr, "BFCB", taglen) == 0) 
            break; // found start of a new block, shouldn't have happend

        if (bFnd)
            continue;

        {
            // for debugging, found something didn't expect, will mean missing a tag to skip in the lists
            // std::string str = "1234";
            // memcpy(str.data(), searchPtr, 4);
            // str += "\n";
            // dbgout(str); 
        }

        searchPtr++;
    }
}

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

    // do merging local data to global at end to allow multi-thread to work effectively
    if (!oLocalstdts.empty() || !oLocalpndts.empty() || !oLocallctns.empty())
    {   
        std::lock_guard<std::mutex> guard(m_output_mutex);

        if (eType == eESP_STDT) for (auto& oSTDTRec : oLocalstdts) m_stdts.push_back(oSTDTRec);
        if (eType == eESP_PNDT) for (auto& oPNDTRec : oLocalpndts) m_pndts.push_back(oPNDTRec);
        if (eType == eESP_LCTN) for (auto& oLCTNRec : oLocallctns) m_lctns.push_back(oLCTNRec);

        if (!oLocalFmIdMap.empty())
            mergeFmIDMaps(m_FmIDMap, oLocalFmIdMap);
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

void CEsp::_dopndt_op_findparts(PNDTrec& oRec, const char*& searchPtr, const char*& endPtr)
{
    bool bFndBdst = false;
    size_t taglen = 4;
    oRec.m_pEdid = &BADEDIDREC;
    oRec.m_pAnam = &BADPNDTANAMREC;
    oRec.m_pGnam = &BADPNDTGNAMREC;
    oRec.m_isBad = true;
    oRec.m_isMissingMatchingBfce = false;
    oRec.m_oComp.clear();

    // Find the EDID
    // TODO template This kind of operation
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

    //  Find BDST tag first as ANAM and GNAM can appear in mutliple places, we need ones in BDST section
    while (searchPtr < endPtr)
    {
        if (BLEFT >= taglen)
        { 
            if (memcmp(searchPtr, "BFCB", taglen) == 0)
            {
                if (BLEFT >= sizeof(BFCBrecOv) + sizeof(BFCBDatarecOv))
                {
                    // Currently treating BFCB-BFBE records as optional since a stdt/pndt can be created this way in ck
                    const BFCBrecOv *pBfcb = reinterpret_cast<const BFCBrecOv*>(searchPtr);
                    searchPtr += BSKIP(pBfcb); // Skip forward
                    const BFCBDatarecOv *pBfcbData = reinterpret_cast<const BFCBDatarecOv*>(searchPtr);
                    searchPtr += BSKIP(pBfcbData); // Skip forward

                    if (oRec.m_oComp.size()<LIMITCOMPSTO) // FORCE A LIMIT
                        if (oRec.m_oComp.size()<=MAXCOMPINREC) // Data is bad if above this
                            oRec.m_oComp.push_back(COMPRec(pBfcb, pBfcbData)); 

                    // Skip stuff we are not intersted in the component, faster than looking for BDCE end tag
                    _doBfcbquickskip(searchPtr, endPtr);

                    if (memcmp(searchPtr, "BFCB", taglen) == 0)
                    {
                        // record that is missing the BFCE end of block record mark it as bad
                        // bFndMissingBfce = true; -ignore for now about 100 planets have this issue.
                        oRec.m_isMissingMatchingBfce = true;
                        continue; 
                    }

                    if (BLEFT >= sizeof(BFCErecOv))
                    {
                        // Ignore the end marker for now, it contains no useful data
                        searchPtr += sizeof(BFCErecOv); // Skip forward
                    }
                    continue; 
                }
            }
            else
            if (memcmp(searchPtr, "BDST", taglen) == 0)
            {
                bFndBdst = true;
                break; // Done
            }
        }
        searchPtr++;
    }

    if (bFndBdst) // If did not find it, then it's a bad record, don't do more work
    {
        while (searchPtr < endPtr)
        {
            if (BLEFT >= taglen)
            {
                if (memcmp(searchPtr, "ANAM", taglen) == 0)
                {
                    if (BLEFT >= sizeof(PNDTAnamOv))
                    {
                        oRec.m_pAnam = reinterpret_cast<const PNDTAnamOv*>(searchPtr);
                        searchPtr += BSKIP(oRec.m_pAnam); // Skip forward
                    }
                    continue;
                }
                else
                if (memcmp(searchPtr, "GNAM", taglen) == 0)
                {
                    if (BLEFT >= sizeof(PNDTGnamOv))
                    {
                        oRec.m_pGnam = reinterpret_cast<const PNDTGnamOv*>(searchPtr);
                        searchPtr += BSKIP(oRec.m_pGnam); // Skip forward
                    }
                    break; // Done - Not looking for anything beyond this
                }
            }
            searchPtr++;
        }
    }

    // check if record okay
    if (oRec.m_pEdid->m_size && oRec.m_pEdid->m_name && oRec.m_pAnam->m_size && oRec.m_pAnam->m_aname && oRec.m_pGnam->m_size)
        oRec.m_isBad = false;
}

// Do all the work to build out a PNDT record data structure
// It is compressed so decompressing is most of the effort
void CEsp::_dopndt_op(size_t iPndtIdx)
{
    PNDTrec& oRec = m_pndts[iPndtIdx];
    decompress_data(oRec.m_pcompdata, oRec.m_compdatasize, oRec.m_decompdata, oRec.m_pHdr->m_decompsize);

    // TODO: build out other PNDT data records
    const char* searchPtr = &oRec.m_decompdata[0];
    const char* endPtr = &oRec.m_decompdata[oRec.m_decompdata.size() - 1];

    _dopndt_op_findparts(oRec, searchPtr, endPtr);
    if (oRec.m_isMissingMatchingBfce)
    {
        std::lock_guard<std::mutex> guard(m_output_mutex);
        m_MissingBfceMap[oRec.m_pHdr->m_formid] = GENrec(eESP_PNDT, iPndtIdx);
    }
    else
    if (oRec.m_isBad)
    {
        // Add to bad list
        std::lock_guard<std::mutex> guard(m_output_mutex);
        m_BadMap[oRec.m_pHdr->m_formid] = GENrec(eESP_PNDT, iPndtIdx);
    }
    else
    {
        // Add to star system id index
        std::lock_guard<std::mutex> guard(m_output_mutex);
        m_SystemIDMap[oRec.m_pGnam->m_systemId].push_back(GENrec(eESP_PNDT, iPndtIdx));
    }
}

void CEsp::process_pndt_ranged_op_mt(size_t start, size_t end)
{
    for (size_t i = start; i < end; ++i)
        _dopndt_op(i);
}

// multi-tread process for getting PNDT data from compressed streams
void CEsp::dopndt_op_mt()
{
    size_t num_pointers = m_pndts.size();
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
            futures.push_back(std::async(std::launch::async, &CEsp::process_pndt_ranged_op_mt, this, start, end));
    }

    // Wait for all threads to complete
    for (auto& future : futures)
        future.get();
}

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
                    // Currently treating BFCB-BFBE records as optional since a stdt/pndt can be created this way in ck
                    const BFCBrecOv *pBfcb = reinterpret_cast<const BFCBrecOv*>(searchPtr);
                    searchPtr += BSKIP(pBfcb); // Skip forward
                    const BFCBDatarecOv *pBfcbData = reinterpret_cast<const BFCBDatarecOv*>(searchPtr);
                    searchPtr += BSKIP(pBfcbData); // Skip forward

                    if (oRec.m_oComp.size()<LIMITCOMPSTO) // FORCE A LIMIT, we don't need all the component records there can be >1.5k of these
                        if (oRec.m_oComp.size()<=MAXCOMPINREC) // Data is bad if above this
                            oRec.m_oComp.push_back(COMPRec(pBfcb, pBfcbData)); 

                    // Skip stuff we are not intersted in the component, faster than looking for BDCE end tag
                    _doBfcbquickskip(searchPtr, endPtr);

                    if (BLEFT >= sizeof(BFCErecOv))
                    {
                        if (memcmp(searchPtr, "BFCB", taglen) == 0)
                        {
                            // record that is missing the BFCE end of block record mark it as bad
                            oRec.m_isMissingMatchingBfce = true;
                            continue;
                        }

                        if (BLEFT >= sizeof(BFCErecOv))
                        {
                            // Ignore the end marker for now, it contains no useful data
                            searchPtr += sizeof(BFCErecOv); // Skip forward
                        }
                    }
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

    // DEBUG interupt
    // if (oRec.m_pHdr->m_formid==0x3D9E13)
    // {
    //    std::string str = _dumpKeywords(oRec.m_pKsiz, oRec.m_pKwda) + "\n";
    //    dbgout(str);
    // }

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

// Used to provide basic info to be used in the UX
// provides info for one record of type eType and positon iIdx in the collection
bool CEsp::getBasicInfo(ESPRECTYPE eType, size_t iIdx, BasicInfoRec& oBasicInfoRec)
{
    oBasicInfoRec.clear();
    switch (eType) 
    {
        case eESP_STDT:
            if (iIdx >= 0 && iIdx < m_stdts.size() && !m_stdts[iIdx].m_isBad)
            {
                size_t iPlayerLvl = 0;
                size_t iPlayerLvlMax = 255;
                findPlayerLvl(m_stdts[iIdx], iPlayerLvl, iPlayerLvlMax);
                oBasicInfoRec = BasicInfoRec(eType,
                    (const char*)&m_stdts[iIdx].m_pEdid->m_name,
                    (const char*)&m_stdts[iIdx].m_pAnam->m_aname, false,
                    m_stdts[iIdx].getfPos(), iIdx, 0, 0, iPlayerLvl, iPlayerLvlMax);
                return true;
            }
            break;

        case eESP_PNDT:
            if (iIdx >= 0 && iIdx < m_pndts.size() && !m_pndts[iIdx].m_isBad)
            {
                fPos oSystemPosition;

                // Find the Primary star of the planet if it exists
                // TODO: handle case if Primary is a planet
                size_t iPrimaryIdx = findPrimaryIdx(iIdx, oSystemPosition);
                oBasicInfoRec = BasicInfoRec(eType, 
                    (const char*)&m_pndts[iIdx].m_pEdid->m_name, 
                    (const char*)&m_pndts[iIdx].m_pAnam->m_aname, 
                    m_pndts[iIdx].m_pGnam->m_primePndtId != 0, oSystemPosition, iIdx, iPrimaryIdx);
                return true;
            }
            break;

        case eESP_LCTN:
            if (iIdx >= 0 && iIdx < m_lctns.size() && !m_lctns[iIdx].m_isBad)
            {
                fPos oNoPos(0,0,0);
                oBasicInfoRec = BasicInfoRec(eType, 
                    (const char*)&m_lctns[iIdx].m_pEdid->m_name, 
                    (const char*)&m_lctns[iIdx].m_pAnam->m_aname, 
                    0, oNoPos, iIdx, 0, m_lctns[iIdx].m_pData->m_playerLvl, m_lctns[iIdx].m_pData->m_playerLvlMax);
                return true;
            }
            break;
    }

    return false;
}

// Get all the basic info records for the planets oribiting the passed Primary
// TODO: support passing a planet as a primary
void CEsp::getBasicInfoRecsOrbitingPrimary(CEsp::ESPRECTYPE eType, CEsp::formid_t iPrimary, std::vector<CEsp::BasicInfoRec>& oBasicInfos, bool bIncludeMoons)
{
    oBasicInfos.clear();
    oBasicInfos.shrink_to_fit();

    switch (eType) {
    case eESP_STDT:
    {
        // Find planets given a primary star, get star system id from star
        formid_t iSystemId = m_stdts[iPrimary].m_pDnam->m_systemId;
        auto it = m_SystemIDMap.find(iSystemId);
        if (it != m_SystemIDMap.end())
        {
            std::vector<GENrec>& genRecords = it->second;
            for (const GENrec& oRec : genRecords)
            {
                if (oRec.m_eType == eESP_PNDT) // Find planets in same star system
                {
                    bool bIsMoon = m_pndts[oRec.m_iIdx].m_pGnam->m_primePndtId != 0;
                    if (bIncludeMoons || !bIsMoon)
                        oBasicInfos.push_back(BasicInfoRec(eESP_PNDT, 
                            (const char*)&m_pndts[oRec.m_iIdx].m_pEdid->m_name, 
                            (const char*)&m_pndts[oRec.m_iIdx].m_pAnam->m_aname,
                            bIsMoon, m_stdts[iPrimary].getfPos(), oRec.m_iIdx, iPrimary));
                }
            }
        }
    }
    break;

    case eESP_PNDT:
        // TODO:
        break;
    }
}

// Function to calculate the Euclidean distance between two positions
float CEsp::calcDist(const CEsp::fPos& p1, const CEsp::fPos& p2) {
    return std::sqrt((p2.m_xPos - p1.m_xPos) * (p2.m_xPos - p1.m_xPos) +
        (p2.m_yPos - p1.m_yPos) * (p2.m_yPos - p1.m_yPos) +
        (p2.m_zPos - p1.m_zPos) * (p2.m_zPos - p1.m_zPos));
}

// Function to find the closest distance from a target position to any star in a list
float CEsp::findClosestDist(const size_t iSelfIdx, const CEsp::fPos& targetPos, const std::vector<CEsp::BasicInfoRec>& oBasicInfoRecs, size_t& idx)
{
    float minDistance = std::numeric_limits<float>::max(); // Initialize with maximum float value

    for (const CEsp::BasicInfoRec& oBasicRec : oBasicInfoRecs)
    {
        if (oBasicRec.m_iIdx != iSelfIdx) // Dont check distance with self
        {
            float distance = calcDist(targetPos, oBasicRec.m_StarMapPostion);
            if (distance < minDistance)
            {
                idx = oBasicRec.m_iIdx;
                minDistance = distance;
            }
        }
    }

    return minDistance;
}

float CEsp::getMinDistance(float fMinDistance)
{
    std::vector<BasicInfoRec> oBasicInfoRecs;
    getBasicInfoRecs(eESP_STDT, oBasicInfoRecs);
    size_t iFndMinIdx;

    for (const BasicInfoRec& oBasicInfo : oBasicInfoRecs)
    {
        float fDist = findClosestDist(oBasicInfo.m_iIdx, oBasicInfo.m_StarMapPostion, oBasicInfoRecs, iFndMinIdx);
        if (fDist < fMinDistance)
            fMinDistance = fDist;
    }
    return fMinDistance;
}

bool CEsp::checkMinDistance(const fPos& ofPos, float fMinDistance, std::string &strErr)
{
    std::vector<BasicInfoRec> oBasicInfoRecs;
    getBasicInfoRecs(eESP_STDT, oBasicInfoRecs);
    size_t iFndMinIdx;

    float fdistance = findClosestDist(NO_RECIDX, ofPos, oBasicInfoRecs, iFndMinIdx);
    if (fdistance < fMinDistance)
    {
        if (fdistance < 0.000000001)
            strErr = "The star position is the same as an existing star.";
        else
        {
            std::string str = "(unknown)";
            CEsp::BasicInfoRec oBasicRec;
            if (getBasicInfo(eESP_STDT, iFndMinIdx, oBasicRec))
                str = oBasicRec.m_pName;
            strErr = "The star position is very close to " + str + " with a distance of " + std::to_string(fdistance) + ".";
        }
        return false;
    }
    return true;
}

CEsp::fPos CEsp::findCentre()
{
    std::vector<BasicInfoRec> oBasicInfoRecs;
    getBasicInfoRecs(eESP_STDT, oBasicInfoRecs);

    if (oBasicInfoRecs.empty())
        return fPos(); 

    size_t numPositions = oBasicInfoRecs.size();
    float sumX = 0.0f;
    float sumY = 0.0f;
    float sumZ = 0.0f;

    for (const BasicInfoRec& oBasicInfo : oBasicInfoRecs) 
    {
        sumX += oBasicInfo.m_StarMapPostion.m_xPos;
        sumY += oBasicInfo.m_StarMapPostion.m_yPos;
        sumZ += oBasicInfo.m_StarMapPostion.m_zPos;
    }

    return fPos(sumX / numPositions,  sumY / numPositions, sumZ / numPositions);
}

CEsp::fPos CEsp::posSwap(const CEsp::fPos& oOrgPos, CEsp::POSSWAP eSwapXZ)
{
    fPos oPos = oOrgPos;

    switch (eSwapXZ) {
    case PSWAP_XZ:     oPos = fPos(oOrgPos.m_zPos, oOrgPos.m_yPos, oOrgPos.m_xPos); break;
    case PSWAP_XY:     oPos = fPos(oOrgPos.m_yPos, oOrgPos.m_xPos, oOrgPos.m_zPos); break;
    case PSWAP_YZ:     oPos = fPos(oOrgPos.m_xPos, oOrgPos.m_zPos, oOrgPos.m_yPos); break;
    case PSWAP_XFLIP:  oPos = fPos(-oOrgPos.m_xPos, oOrgPos.m_yPos, oOrgPos.m_zPos); break;
    case PSWAP_YFLIP:  oPos = fPos(oOrgPos.m_xPos, -oOrgPos.m_yPos, oOrgPos.m_zPos); break;
    case PSWAP_ZFLIP:  oPos = fPos(oOrgPos.m_xPos, oOrgPos.m_yPos, oOrgPos.m_zPos); break;
    default:
        return oOrgPos;
    }

    // reverse them so they plot correctly on screen axis
    oPos = fPos(-oPos.m_xPos, -oPos.m_yPos, -oPos.m_zPos);
    return oPos;
}


void CEsp::getStarPositons(std::vector<CEsp::StarPlotData>& oStarPlots, CEsp::fPos& min, CEsp::fPos& max, CEsp::POSSWAP eSwap = CEsp::PSWAP_NONE)
{
    std::vector<BasicInfoRec> oBasicInfoRecs;
    getBasicInfoRecs(eESP_STDT, oBasicInfoRecs);
    for (const BasicInfoRec& oBasicInfo : oBasicInfoRecs)
    {
        // Do swaps if requested 
        fPos oPos = posSwap(oBasicInfo.m_StarMapPostion, eSwap);

        // Set min and max
        if (oPos.m_xPos < min.m_xPos) min.m_xPos = oPos.m_xPos;
        if (oPos.m_yPos < min.m_yPos) min.m_yPos = oPos.m_yPos;
        if (oPos.m_zPos < min.m_zPos) min.m_zPos = oPos.m_zPos;
        if (oPos.m_xPos > max.m_xPos) max.m_xPos = oPos.m_xPos;
        if (oPos.m_yPos > max.m_yPos) max.m_yPos = oPos.m_yPos;
        if (oPos.m_zPos > max.m_zPos) max.m_zPos = oPos.m_zPos;

        oStarPlots.push_back(StarPlotData(oPos, oBasicInfo.m_pAName));
    }
}

// Get all the basic info records given the type
void CEsp::getBasicInfoRecs(CEsp::ESPRECTYPE eType, std::vector<CEsp::BasicInfoRec>& oBasicInfos)
{
    oBasicInfos.clear();
    size_t iCount = getNum(eType);
    for (size_t i = 0; i < iCount; ++i)
    {
        BasicInfoRec oBasicInfoRec;
        if (getBasicInfo(eType, i, oBasicInfoRec))
            oBasicInfos.push_back(oBasicInfoRec);
    }
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

    timept ots = startTC();
    if (!loadhdrs())
    {
        strErr = "Failed to build header structures.";
        return false;
    }
    endTC("loadhdrs", ots);

    // Build the hdrs first
    ots = startTC();
    do_process_subrecs_mt();
    endTC("do_process_subrecs_mt", ots);

    // Build out records beyond just the header information using multiple threads
    ots = startTC();
    dostdt_op_mt();
    endTC("dostdt_op_mt", ots);

    ots = startTC();
    dopndt_op_mt();
    endTC("dopndt_op_mt", ots);

    ots = startTC();
    dolctn_op_mt();
    do_process_lctns();
    endTC("dolctn_op_mt", ots);

    return true;
}


// Load the ESP or ESM file
bool CEsp::load(const std::wstring &strFileName, std::string &strErr)
{
    m_wstrfilename = strFileName;

    timept ots = startTC();
    if (!loadfile(strErr))
        return false;
    endTC("loadfile", ots);

    if (!_loadfrombuffer(strErr))
        return false;

    return true;
}
