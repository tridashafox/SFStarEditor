#include "espmanger.h"
#ifdef _DEBUG
#include <comdef.h>  
#include <shlobj.h>  
#pragma comment(lib, "Shcore.lib")
#endif 

// InfoShow 
std::string CEsp::infshEspData()
{
    std::string strfile = getFnameAsStr() + ".txt";
    infshtofile(strfile);
    return strfile;
}

std::string CEsp::_infshComps(const std::vector<CEsp::COMPRec> &oComps)
{
    std::string str = " [Comps: count: " + std::to_string(oComps.size()) + " ";
    if (!oComps.empty())
    {
        size_t iLimit = oComps.size() <= MAXCOMPFORDUMPING ? oComps.size() : MAXCOMPFORDUMPING; // limit the outputed size
        for (size_t i = 0; i < iLimit; ++i)
            str += std::string((char*)&oComps[i].m_pBfcbName->m_name) + ", ";
        rlc(str);
        if (oComps.size() > MAXCOMPFORDUMPING)
            str += "...(more)";
    }
    else
        str += "(none)";

    return str + "]";
}

std::string CEsp::_infshKeywords(const CEsp::KSIZrecOv *pKsiz, const CEsp::KWDArecOv *pKwda)
{
    std::string str = " [KSIZ: count: " + std::to_string(pKsiz->m_count) + " KWDA: ";
    for (size_t i = 0; i < pKsiz->m_count; ++i)
    {
        const uint32_t* pdata = (uint32_t*)&pKwda->m_data;
        str += std::format("{:08X}, ", pdata[i]);
    }
    rlc(str);

    return str + "]";
}

std::string CEsp::_infshStdt(const CEsp::STDTrec& oRec)
{
    char pTagBuff[5] = { '\0','\0','\0','\0','\0' };

    memcpy(pTagBuff, oRec.m_pHdr->m_STDTtag, sizeof(pTagBuff) - 1);
    std::string strname((char*)&oRec.m_pEdid->m_name);  if (strname.empty()) strname = "(none)";
    std::string stranam((char*)&oRec.m_pAnam->m_aname); if (stranam.empty()) stranam = "(none)";
    std::string str = std::string(pTagBuff) + std::format(" [0x{:08X}]", oRec.m_pHdr->m_formid) + " [Size: "
        + std::format("0x{:08X}", oRec.m_pHdr->m_size) + "] [NAME: " + strname + "] [ANAM: " + stranam
        + std::format("] [Location: ({},{},{})]", oRec.getfPos().m_xPos, oRec.getfPos().m_yPos, oRec.getfPos().m_zPos)
        + std::format(" [StarSystem: 0x{:08X}]", oRec.m_pDnam->m_systemId)
        + "[CatalogID: " + cbToStr(oRec.m_oBGSStarDataCompStrings.m_pCatalogID) + "] "
        + "[SpectralClass: " + cbToStr(oRec.m_oBGSStarDataCompStrings.m_pSpectralClass) + "] "
        + (oRec.m_pBGSStarDataCompInfo ? "[Radius: " + std::to_string(oRec.m_pBGSStarDataCompInfo->m_Radius) + "]" : "");
    str += _infshComps(oRec.m_oComp);
    return str;
}

std::string CEsp::_infshPndt(const CEsp::PNDTrec& oRec)
{
    char pTagBuff[5] = { '\0','\0','\0','\0','\0' };

    memcpy(pTagBuff, oRec.m_pHdr->m_PNDTtag, sizeof(pTagBuff) - 1);
    std::string strname((char*)&oRec.m_pEdid->m_name);  if (strname.empty()) strname = "(none)";
    std::string stranam((char*)&oRec.m_pAnam->m_aname); if (stranam.empty()) stranam = "(none)";
    std::string str = std::string(pTagBuff) + std::format(" [0x{:08X}]", oRec.m_pHdr->m_formid) + " [Size: "
        + std::format("0x{:08X}", oRec.m_pHdr->m_size) + "] [NAME: " + strname + "] [ANAM: " + stranam
        + std::format("] [StarSystem: 0x{:08X}]", oRec.m_pGnam->m_systemId)
        + " [perihelion: " + std::to_string(oRec.m_pHnam3->m_perihelion) + "]";
    for (int i = 0; i < NUMHNAMSTRINGS; i++)
        if (oRec.m_oHnam2.m_Strings[i] && oRec.m_oHnam2.m_Strings[i]->m_size)
            str += " [" + PNDTHnam2StringLabel[i] + ": " + std::string(&oRec.m_oHnam2.m_Strings[i]->m_string) + "]";
    str += _infshComps(oRec.m_oComp);
    return str;
}

std::string CEsp::_infshLctn(const CEsp::LCTNrec& oRec)
{
    char pTagBuff[5] = { '\0','\0','\0','\0','\0' };

    memcpy(pTagBuff, oRec.m_pHdr->m_LCTNtag, sizeof(pTagBuff) - 1);
    std::string strname((char*)&oRec.m_pEdid->m_name);  if (strname.empty()) strname = "(none)";
    std::string stranam((char*)&oRec.m_pAnam->m_aname); if (stranam.empty()) stranam = "(none)";
    std::string strfull((char*)&oRec.m_pFull->m_name);  if (strfull.empty()) strfull = "(none)";
    std::string str = std::string(pTagBuff) + std::format(" [0x{:08X}]", oRec.m_pHdr->m_formid) + + " [Size: " + 
        std::format("0x{:08X}", oRec.m_pHdr->m_size) + "] [NAME: " + strname + "] [ANAM: " + stranam;
    if (isESM())
        str += std::format("[FULL: 0x{:04X}", static_cast<unsigned char>(oRec.m_pFull->m_name)) + "]";
    else
    {
        std::string strfull((char*)&oRec.m_pFull->m_name);  if (strfull.empty()) strfull = "(none)";
        str += "[FULL: " + strfull + "] ";
    }
    str += " [DATA: Min level:" + std::to_string(oRec.m_pData->m_playerLvl) + ", Max level: " + std::to_string(oRec.m_pData->m_playerLvlMax) + ", Faction: ";
    str += oRec.m_pData->m_faction ? std::format("0x{:04X}", oRec.m_pData->m_faction) +"]" : "none]";
    str += _infshKeywords(oRec.m_pKsiz, oRec.m_pKwda);
    return str;
}

void CEsp::_infshVector(const std::vector<char>&oV, std::string strNamepostfix)
{
    #ifdef _DEBUG
    PWSTR path = nullptr;
    if (!(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &path))))
        return;

    _bstr_t bstrPath(path);
    std::string strpath = (const char*)bstrPath;
    CoTaskMemFree(path);

    std::string strErr;
    std::string strDebugOutfn = strpath + "\\Vector_" + getFnameRoot() + "_" + strNamepostfix + ".bin";
    std::filesystem::path filePath(strDebugOutfn);
    std::wstring wstrFilename = filePath.wstring();

    _saveToFile(oV, wstrFilename, strErr);
    #endif
}

void CEsp::_infshToFile(const std::vector<std::string>& oOutputs, const std::string& fileName, bool bAppend = false) 
{
    // Open the file in write mode
    std::ofstream outFile;
    if (bAppend) 
        outFile.open(fileName, std::ios::app); // Append mode
    else 
        outFile.open(fileName); // overwrite Write mode (default)

    if (!outFile) 
        return;
    for (const auto& line : oOutputs) 
        outFile << line << std::endl;
    outFile.close();
}

void CEsp::infshtofile(const std::string& fileName)
{
    std::vector<std::string> oOutputs;

    for (size_t i = 0; i < m_stdts.size(); ++i)
        oOutputs.push_back(_infshStdt(m_stdts[i]));
    for (size_t i = 0; i < m_pndts.size(); ++i)
        oOutputs.push_back(_infshPndt(m_pndts[i]));
    for (size_t i = 0; i < m_lctns.size(); ++i)
        oOutputs.push_back(_infshLctn(m_lctns[i]));

    std::sort(oOutputs.begin(), oOutputs.end());
    _infshToFile(oOutputs, fileName);

    oOutputs.clear();
    infshBadRecs(oOutputs);
    if (oOutputs.size())
    {
        std::sort(oOutputs.begin(), oOutputs.end());
        oOutputs.insert(oOutputs.begin(), " ");
        oOutputs.insert(oOutputs.begin(), "Bad records:");
        oOutputs.insert(oOutputs.begin(), "=====================================================");
        _infshToFile(oOutputs, fileName, true);
    }

    oOutputs.clear();
    infshMissingBfceMapRecs(oOutputs);
    if (oOutputs.size())
    {
        std::sort(oOutputs.begin(), oOutputs.end());
        oOutputs.insert(oOutputs.begin(), " ");
        oOutputs.insert(oOutputs.begin(), "Records containg a BFCB with no matching BFCE marker:");
        oOutputs.insert(oOutputs.begin(), "=====================================================");
        _infshToFile(oOutputs, fileName, true);
    }
}

std::string CEsp::infshStats()
{
    CHAR szStatusTextSrc[GENBUFFSIZE]{};

    sprintf_s(szStatusTextSrc, sizeof(szStatusTextSrc) / sizeof(szStatusTextSrc[0]),
        "STDT(%zu:%zu), PNDT(%zu:%zu), LCTN(%zu:%zu)",
        getGrupNum(CEsp::eESP_STDT), getNum(CEsp::eESP_STDT),
        getGrupNum(CEsp::eESP_PNDT), getNum(CEsp::eESP_PNDT),
        getGrupNum(CEsp::eESP_LCTN), getNum(CEsp::eESP_LCTN));

    return std::string(szStatusTextSrc);
}

size_t CEsp::infshBadRecs(std::vector<std::string>&oOutputs)
{
    oOutputs.clear();
    std::string strpref = "Bad Record Found: ";

    for (const auto& pair : m_BadMap)
    {
        const GENrec& ogenrec = pair.second;
        switch (ogenrec.m_eType)
        {
            case eESP_STDT:
                oOutputs.push_back(strpref + _infshStdt(m_stdts[ogenrec.m_iIdx]));
                break;

            case eESP_PNDT:
                oOutputs.push_back(strpref + _infshPndt(m_pndts[ogenrec.m_iIdx]));
                break;

            case eESP_LCTN:
                oOutputs.push_back(strpref + _infshLctn(m_lctns[ogenrec.m_iIdx]));
                break;
        }
    }

    return m_BadMap.size();
}

size_t CEsp::infshMissingBfceMapRecs(std::vector<std::string>&oOutputs)
{
    oOutputs.clear();
    std::string strpref = "Records Missing BFCE ends: ";

    for (const auto& pair : m_MissingBfceMap)
    {
        const GENrec& ogenrec = pair.second;
        switch (ogenrec.m_eType)
        {
            case eESP_STDT:
                oOutputs.push_back(strpref + _infshStdt(m_stdts[ogenrec.m_iIdx]));
                break;

            case eESP_PNDT:
                oOutputs.push_back(strpref + _infshPndt(m_pndts[ogenrec.m_iIdx]));
                break;

            case eESP_LCTN:
                oOutputs.push_back(strpref + _infshLctn(m_lctns[ogenrec.m_iIdx]));
                break;
        }
    }
    
    return m_BadMap.size();
}

void CEsp::infshPlanetPositions(size_t iStarIdx, std::string& strOut)
{
    std::vector<BasicInfoRec> oBasc;
    BasicInfoRec oBasicInfoNewPlanet;
    getBasicInfoRecsOrbitingPrimary(CEsp::eESP_STDT, iStarIdx, oBasc, true, true);
    std::sort(oBasc.begin(), oBasc.end(),
        [](const CEsp::BasicInfoRec& a, const CEsp::BasicInfoRec& b) { return a.m_iPlanetlocalId < b.m_iPlanetlocalId; });
    std::string strdmpout = "(";
    for (size_t i = 0; i < oBasc.size(); ++i)
        strdmpout += std::string(oBasc[i].m_pAName) + ": "
        + std::to_string(oBasc[i].m_iPlanetlocalId) + (oBasc[i].m_bIsMoon ? "m-" + std::to_string(oBasc[i].m_iParentlocalId) : "p")
        + std::string((i + 1 < oBasc.size()) ? ", " : "");

    strOut = strdmpout + ")";
}
