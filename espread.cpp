#include "espmanger.h"

const char* SZBADRECORD = "";

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