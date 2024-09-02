#include "espmanger.h"

// Reading common functions

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
    if (bFnd = (m_FmIDMap.find(formid) != m_FmIDMap.end()))
        fndrec = m_FmIDMap[formid];

    return bFnd;
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
bool CEsp::findLocInfo(const STDTrec &oRecStar, size_t &iPlayerLvl, size_t &iPlayerLvlMax, size_t &iFaction)
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
                    iFaction = m_lctns[oGen.m_iIdx].m_pData->m_faction;
                    return true;
                }
            }
    }

    return false;
}

// Given index to a planet, finds what it is orbiting
size_t CEsp::findPrimaryIdx(size_t iIdx)
{
    fPos oSystemPosition;
    return findPrimaryIdx(iIdx, oSystemPosition);
}

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

// pulls togather info from star and locations to create star basic info rec give index to planet in m_stdts 
CEsp::BasicInfoRec CEsp::_makeBasicStarRec(const size_t iIdx)
{
    size_t iPlayerLvl = MIN_PLAYERLEVEL;
    size_t iPlayerLvlMax = MAX_PLAYERLEVEL;
    size_t iFaction = NO_FACTION;

    findLocInfo(m_stdts[iIdx], iPlayerLvl, iPlayerLvlMax, iFaction);

    return BasicInfoRec(eESP_STDT,
        (const char*)&m_stdts[iIdx].m_pEdid->m_name,
        (const char*)&m_stdts[iIdx].m_pAnam->m_aname,
        false, false,
        m_stdts[iIdx].getfPos(), iIdx, NO_ORBIT, NO_PLANETPOS, NO_PARENTLOCALID,
        iPlayerLvl, iPlayerLvlMax, iFaction);
}

// pulls togather info from star and locations to create planet basic info rec give index to planet in m_pndts 
CEsp::BasicInfoRec CEsp::_makeBasicPlanetRec(const size_t iIdx)
{
    size_t iPlayerLvl = MIN_PLAYERLEVEL;
    size_t iPlayerLvlMax = MAX_PLAYERLEVEL;
    size_t iFaction = NO_FACTION;

    fPos oSystemPosition;
    size_t iPrimaryIdx = findPrimaryIdx(iIdx, oSystemPosition);

    bool bisMoon = m_pndts[iIdx].m_pGnam->m_parentPndtLocalId; // Planet as a primary planet id it is a moom
    bool bIsLandable = m_pndts[iIdx].m_oPpbds.size() != 0; // Planet has  biom records, it is landable

    // Find the Primary star of the planet if it exists and not a moon
    if (!bisMoon && iPrimaryIdx < m_stdts.size()) // find the lvls if valid primary index
        findLocInfo(m_stdts[iPrimaryIdx], iPlayerLvl, iPlayerLvlMax, iFaction);


    return BasicInfoRec(eESP_PNDT,
        (const char*)&m_pndts[iIdx].m_pEdid->m_name,
        (const char*)&m_pndts[iIdx].m_pAnam->m_aname,
        bisMoon, bIsLandable,
        oSystemPosition, iIdx, iPrimaryIdx, m_pndts[iIdx].m_pGnam->m_PndtLocalId, m_pndts[iIdx].m_pGnam->m_parentPndtLocalId,
        iPlayerLvl, iPlayerLvlMax, iFaction);
}

CEsp::BasicInfoRec CEsp::_makeBasicLocRec(const size_t iIdx)
{
    // TODO get fpos info from assoicated star (not sure it's needed anywhere at the moment
    // would be the reverse of findLocInfo

    return BasicInfoRec(eESP_LCTN,
        (const char*)&m_lctns[iIdx].m_pEdid->m_name,
        (const char*)&m_lctns[iIdx].m_pAnam->m_aname,
        false, false,
        NO_FPOS, iIdx, NO_ORBIT, NO_PLANETPOS, NO_PARENTLOCALID,
        m_lctns[iIdx].m_pData->m_playerLvl, m_lctns[iIdx].m_pData->m_playerLvlMax, m_lctns[iIdx].m_pData->m_faction);
}


// Used to provide basic info to be used in the UX
// provides info for one record of type eType and positon iIdx in the collection

bool CEsp::getBasicInfo(formid_t iSystemId, size_t iParentPlanetID, BasicInfoRec& oBasicInfoRec)
{
    if (!iSystemId || !iParentPlanetID || iParentPlanetID==NO_PARENTLOCALID)
        return false;


    auto it = m_SystemIDMap.find(iSystemId);
    if (it == m_SystemIDMap.end())
        return false;

    std::vector<GENrec>& genRecords = it->second;
    for (const GENrec& oGen : genRecords)
    {
        if (oGen.m_eType == eESP_PNDT && m_pndts[oGen.m_iIdx].m_pGnam && m_pndts[oGen.m_iIdx].m_pGnam->m_PndtLocalId == iParentPlanetID)
            return getBasicInfo(oGen.m_eType, oGen.m_iIdx, oBasicInfoRec);
    }

    return false;
}

bool CEsp::getBasicInfo(ESPRECTYPE eType, formid_t formid, BasicInfoRec& oBasicInfoRec)
{
    if (formid == NO_FORMID)
        return false;

    CEsp::GENrec oGen;
    if (!findFmIDMap(formid, oGen))
        return false;

    return getBasicInfo(oGen.m_eType, oGen.m_iIdx, oBasicInfoRec);

}

bool CEsp::getBasicInfo(ESPRECTYPE eType, size_t iIdx, BasicInfoRec& oBasicInfoRec)
{
    oBasicInfoRec.clear();
    switch (eType) 
    {
        case eESP_STDT:
            if (iIdx >= 0 && iIdx < m_stdts.size() && !m_stdts[iIdx].m_isBad)
            {
                oBasicInfoRec = _makeBasicStarRec(iIdx);
                return true;
            }
            break;

        case eESP_PNDT:
            if (iIdx >= 0 && iIdx < m_pndts.size() && !m_pndts[iIdx].m_isBad)
            {
                oBasicInfoRec = _makeBasicPlanetRec(iIdx);
                return true;
            }
            break;

        case eESP_LCTN:
            if (iIdx >= 0 && iIdx < m_lctns.size() && !m_lctns[iIdx].m_isBad)
            {
                oBasicInfoRec = _makeBasicLocRec(iIdx);
                return true;
            }
            break;
    }

    return false;
}

void CEsp::getMoons(size_t iPlanetIdx, std::vector<BasicInfoRec>& oBasicInfos)
{
    // moon name will be planet name -{a,b,c,d} etc. where a-z is the next available planet LocalId for the planet

    std::vector<BasicInfoRec> oBasicInfosSystem;
    size_t iPrimaryIdx = findPrimaryIdx(iPlanetIdx);
    getBasicInfoRecsOrbitingPrimary(CEsp::eESP_PNDT, iPrimaryIdx, oBasicInfosSystem, true, true); // returns only moons moons for system

    // find moons around specified planet
    for (auto& oBasicInfo : oBasicInfosSystem)
        if (m_pndts[oBasicInfo.m_iIdx].m_pGnam->m_parentPndtLocalId == m_pndts[iPlanetIdx].m_pGnam->m_PndtLocalId)
            oBasicInfos.push_back(oBasicInfo);

    // sort the moons by their LocalId in the system (planet has a parent local id of zero - the star)
    std::sort(oBasicInfos.begin(), oBasicInfos.end(),
        [](const BasicInfoRec& a, const BasicInfoRec& b) {  return a.m_iPlanetlocalId < b.m_iPlanetlocalId; });
}

// Get all the basic info records for the planets oribiting the passed Primary
// If type eESP_STDT is passed returns all items in the star system idenfieid by iPrimary
// if type eESP_PNDT is passed returns all the moons in the star system
void CEsp::getBasicInfoRecsOrbitingPrimary(ESPRECTYPE eType, size_t iPrimary, std::vector<BasicInfoRec>& oBasicInfos, bool bIncludeMoons, bool bIncludeUnlandable)
{
    oBasicInfos.clear();
    oBasicInfos.shrink_to_fit();

    formid_t iSystemId = m_stdts[iPrimary].m_pDnam->m_systemId;
    auto it = m_SystemIDMap.find(iSystemId);
    if (it == m_SystemIDMap.end())
        return;
    std::vector<GENrec>& genRecords = it->second;
    for (const GENrec& oRec : genRecords)
        if (oRec.m_eType == eESP_PNDT) // Find planets in same star system
        {
            bool bIsMoon = m_pndts[oRec.m_iIdx].m_pGnam->m_parentPndtLocalId != 0;
            bool bIsLandable = m_pndts[oRec.m_iIdx].m_oPpbds.size() != 0;

            switch (eType)
            {
                case eESP_STDT:
                    if ((bIncludeMoons || !bIsMoon) && (bIncludeUnlandable || bIsLandable))
                    {
                        BasicInfoRec oBasicInfoPlanet = _makeBasicPlanetRec(oRec.m_iIdx);
                        oBasicInfos.push_back(oBasicInfoPlanet);
                    }
                    break;

                case eESP_PNDT:
                    if (bIsMoon && (bIncludeUnlandable || bIsLandable))
                    {
                        BasicInfoRec oBasicInfoPlanet = _makeBasicPlanetRec(oRec.m_iIdx);
                        oBasicInfos.push_back(oBasicInfoPlanet);
                    }
                    break;
            }
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
    case PSWAP_ZFLIP:  oPos = fPos(oOrgPos.m_xPos, oOrgPos.m_yPos, -oOrgPos.m_zPos); break;
    default:
        return oOrgPos;
    }

    // reverse them so they plot correctly on screen axis
    oPos = fPos(-oPos.m_xPos, -oPos.m_yPos, -oPos.m_zPos);
    return oPos;
}

void CEsp::getPlanetPerihelion(size_t iStarIdx, CEsp::SystemPlotData &oSysPlot, double & min, double & max)
{
    std::vector<BasicInfoRec> oBasicInfoRecs;
    getBasicInfoRecsOrbitingPrimary(eESP_STDT, iStarIdx, oBasicInfoRecs, false, true);

    oSysPlot.m_min = std::numeric_limits<double>::max();
    oSysPlot.m_max = std::numeric_limits<double>::min();
    oSysPlot.m_oPlanetPlots.clear();
    for (const BasicInfoRec& oBasicInfo : oBasicInfoRecs)
    {
        if (!m_pndts[oBasicInfo.m_iIdx].m_pHnam3 || !m_pndts[oBasicInfo.m_iIdx].m_pFnam)
        {
            oSysPlot.m_oPlanetPlots.clear();
            return; // bad
        }

        double fperihelion = m_pndts[oBasicInfo.m_iIdx].m_pHnam3->m_perihelion;
        float fradiusKm = m_pndts[oBasicInfo.m_iIdx].m_pFnam->m_RadiusKm;

        // Set min and max
        if (fperihelion < min) min = fperihelion;
        if (fperihelion > max) max = fperihelion;
        if (fperihelion <  oSysPlot.m_min)  oSysPlot.m_min = fperihelion;
        if (fperihelion >  oSysPlot.m_max)  oSysPlot.m_max = fperihelion;

        oSysPlot.m_oPlanetPlots.push_back(PlanetPlotData(oBasicInfo.m_iIdx, fperihelion, fradiusKm, oBasicInfo.m_pAName));
    }

    // sort by distance
    std::sort(oSysPlot.m_oPlanetPlots.begin(), oSysPlot.m_oPlanetPlots.end(),
        [](const PlanetPlotData& a, const PlanetPlotData& b) { return a.m_fPerihelion < b.m_fPerihelion; });
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
void CEsp::getBasicInfoRecs(CEsp::ESPRECTYPE eType, std::vector<CEsp::BasicInfoRec>& oBasicInfos, bool bExcludeBlanks)
{
    oBasicInfos.clear();
    size_t iCount = getNum(eType);
    for (size_t i = 0; i < iCount; ++i)
    {
        BasicInfoRec oBasicInfoRec;
        if (getBasicInfo(eType, i, oBasicInfoRec))
        {
            // leave out blank records if requested
            if (!bExcludeBlanks || (oBasicInfoRec.m_pName && *oBasicInfoRec.m_pName && oBasicInfoRec.m_pAName && *oBasicInfoRec.m_pAName))
                oBasicInfos.push_back(oBasicInfoRec);
        }
    }
}