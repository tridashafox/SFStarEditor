// espmanger.h: processing of ESM and ESP files
//
// 
// #define STATIC_ZLIB

#define NOMINMAX
#ifdef STATIC_ZLIB
#define ZLIB_H
#endif
#include "framework.h"
#include "sfed.h"
#include "resource.h"
#include <commdlg.h>
#include <shellscalingapi.h>
#include <psapi.h>
#undef NOMINMAX
#include <map>
#include <format>
#include <filesystem>
#include <vector>
#include <fstream>
#include <string>
#include <memory>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <commctrl.h>  
#include <limits> 
#include <thread>
#include <mutex>
#include <future>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <cmath>
#include <chrono>
#ifdef STATIC_ZLIB
#include "zlib\zlib.h"
#endif
#ifndef STATIC_ZLIB
#include <zlib.h>
#endif


#pragma comment(lib, "Shcore.lib")
extern const char* SZBADRECORD;

// Class for processing a .ESP/ESM file
// TODO: Loops need to be decomposing header records and offsets, rather than blindly looking for the 
// four letter tag. For example a name with the text ANAM in it will cause an issue
class CEsp
{
public:
    CEsp() { m_bIsSaved = true; };
    ~CEsp() {};

    enum ESPRECTYPE { eESP_IDK, eESP_PNDT, eESP_STDT, eESP_LCTN };

#define NO_ORBIT -1 
#define NO_RECIDX -1
#define NO_FORMID (0)
#define NO_FPOS CEsp::fPos(std::numeric_limits<float>::min(), std::numeric_limits<float>::min(), std::numeric_limits<float>::min())
#define MIN_PLAYERLEVEL (0)
#define MAX_PLAYERLEVEL (255)
#define NO_FACTION (0)
#define NO_PLANETPOS (0)
#define NO_PARENTLOCALID (0)
#define NO_LOCALID (254)
#define LASTPLANETPOSITION (254)
#define GENBUFFSIZE (1024)
#define STARMAPMAX (30.0) // bounder of starmap planet positions should be within this
#define ESP_FORMIDMASK (0x01FFFFFF) // mask to just affect the top byte of a form id
#define ESP_FORMIDPREF (0x01000000) // looks like this should be 0x01 for ESPs
#define MAXKEYWORDS (255) // There should not be more than 255 keywords in list (protection against bad data)
#define MAXCOMPINREC (3000) // Should not be more than this many components some objects have 1000s of this
#define MAXPPBD (255) // Limit the max number of PPBD records in case there is a data issue they are a set of POIs in planets
#define LIMITCOMPSTO (20) // Currently limiting how many we load to 10, since we only intereted in a few of these.
#define MAXCOMPFORDUMPING 3 // used for to limit dump output
#define BLEFT ((size_t)(endPtr - searchPtr))
#define BSKIP(x) (sizeof(x->m_size)+x->m_size+4)
#define MKFAIL(x) { strErr = x; return false; }

    using formid_t = uint32_t;

    // standrd objects needed for creating data
    const uint32_t KW_LocTypeStarSystem = 0x149F;
    const uint32_t KW_LocTypePlanet = 0x14A0;
    const uint32_t KW_LocTypeMoon = 0x16010;
    const uint32_t KW_LocTypeMajorOribital = 0x70A54;
    const uint32_t KW_LoctTypeSurface = 0x16503;
    const uint32_t KW_LoctTypeOrbit = 0x16504;

    const uint32_t KW_PlanetType03GasGiant = 0x295ed2; // don't have biom

    const formid_t FID_Universe = 0x1A53A;
    const formid_t FID_Debug_ClassMPlanet = 0x5e009;

    // Position of a star system
    struct fPos
    {
        fPos() { clear(); }
        fPos(float x, float y, float z) : m_xPos(x), m_yPos(y), m_zPos(z) {}
        void clear() { m_xPos = m_yPos = m_zPos = 0; }

        float m_xPos;
        float m_yPos;
        float m_zPos;
    };

    // Used to keep the data structures private where they map on to the file data. This provides basic info used by UX
    struct BasicInfoRec
    {
        BasicInfoRec() { clear(); }
        BasicInfoRec(const ESPRECTYPE eType, const char* pName, const char* pAName,
            const bool bMoon, const bool bLandable,
            const fPos& oPos, const size_t iIdx,
            const size_t iPrimaryIdx, const size_t iPlanetLocalId, const size_t iParentLocalId,
            const size_t iSysPlayerLvl, const size_t iSysPlayerLvlMax, const size_t iFaction) :
            m_eType(eType), m_pName(pName), m_pAName(pAName), m_bIsMoon(bMoon), m_bIsLandable(bLandable), m_StarMapPostion(oPos),
            m_iIdx(iIdx), m_iPrimaryIdx(iPrimaryIdx), m_iPlanetlocalId(iPlanetLocalId), m_iParentlocalId(iParentLocalId),
            m_iSysPlayerLvl(iSysPlayerLvl), m_iSysPlayerLvlMax(iSysPlayerLvlMax),
            m_iFaction(iFaction) {
            m_bRtFlag = false;
        }
        void clear()
        {
            m_bRtFlag = false;
            m_pName = m_pAName = SZBADRECORD;
            m_bIsMoon = false;
            m_bIsLandable = true;
            m_StarMapPostion.clear();
            m_eType = eESP_IDK;
            m_iIdx = NO_RECIDX;
            m_iPrimaryIdx = NO_ORBIT;
            m_iPlanetlocalId = 0;
            m_iParentlocalId = 0;
            m_iSysPlayerLvl = 0;
            m_iSysPlayerLvlMax = 0;
            m_iFaction = 0;
        }
        bool m_bRtFlag;        // temp flag for easier data processing
        const char* m_pName;   // Form name
        const char* m_pAName;  // Presentation name
        bool m_bIsMoon;        // If the object is a moon
        bool m_bIsLandable;    // True of the planet can be landed on (it is not a gas giant) - it has a biom file and PPBD records
        fPos m_StarMapPostion; // Position on the star map
        ESPRECTYPE m_eType;    // Type of object
        size_t m_iIdx;         // Idx to core data
        size_t m_iPrimaryIdx;  // Idx to parent star [TODO: Or planet, in which case it will need a eType for the PrimaryIdx]
        size_t m_iPlanetlocalId;   // The position for a planet [TODO: how are moons handled]
        size_t m_iParentlocalId; // for moons which planet by LocalId it is orbiting 
        size_t m_iSysPlayerLvl; // Level of the star system. Used to populate location records and set level
        size_t m_iSysPlayerLvlMax;     // Max level things can be in the location
        size_t m_iFaction;      // faction what it belongs to if any
    };

    // For ploting data on star map
    struct StarPlotData
    {
        StarPlotData() : m_oPos(0, 0, 0), m_strStarName("") {}
        StarPlotData(fPos oPos, std::string strStarName) : m_oPos(oPos), m_strStarName(strStarName) {}
        fPos m_oPos;
        std::string m_strStarName;
    };

     // For ploting data on planet map
    struct PlanetPlotData
    {
        // TODO need planet size
        PlanetPlotData() :m_iPlanetIdx(0), m_fPerihelion(0.0f), m_strName("") {}
        PlanetPlotData(size_t iPlanetIdx, double fPerihelion, std::string strName) : m_iPlanetIdx(m_iPlanetIdx), m_fPerihelion(fPerihelion), m_strName(strName) {}
        double m_fPerihelion;
        std::string m_strName;
        size_t m_iPlanetIdx;
    };

private:
    int no_op() { int a = 0; int b = 1; return a + b; }

    void rlc(std::string& str) // for basic formating of lists 
    {
        if (!str.empty() && str.back() == ' ') str.pop_back();
        if (!str.empty() && str.back() == ',') str.pop_back();
    }

    // For debugging and timing
    void dbgout(const std::string& message) { OutputDebugStringA(message.c_str()); }

    using timept = std::chrono::high_resolution_clock::time_point;

    timept startTC()
    {
        return std::chrono::high_resolution_clock::now();
    }

    void endTC(const std::string& pref, timept startTime)
    {
        timept endTime = std::chrono::high_resolution_clock::now();
        std::chrono::microseconds duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        double milliseconds = duration.count() / 1000.0;
        std::string str = pref + ": " + std::to_string(milliseconds) + " ms.\n";
        dbgout(str);
    }

    // Overlays (Ov) map over the read file data buffer to avoid data copying for performance and size reasons
    // This means they map directly to the file format of the ESM or ESP file
    // Each structure a instance of it named BAD????REC, which is used to point the Ov to before it is mapped to the 
    // file buffer. This means if the mapping can not take place because of bad data, the Ov will overlay valid empty
    // object and valid space. It an also be used to check to see if the OV was mapped sucessfully
    // Note the full size of the record is defined by m_size once set and not the size of the struct. 

    // TODO: this approach has been outgrown, too many records now to use 'patching' style approach
    // Will need to move to using a base class and defining the differnt types of record formats
    // and move to seralization method for data now that the record formats are understood
    // So move away from using the Ov onto the file buffer

#pragma pack(push, 1) // force byte alignment
    struct GenBlock
    {
        GenBlock() { memset(this, 0, sizeof(*this)); }
        uint8_t m_tag[4];
        uint16_t m_size;
        uint8_t m_aname; // first char in string ends with '\0'
    };

    struct TES4HdrOv {
        TES4HdrOv() { memset(this, 0, sizeof(*this)); }
        char m_TES4tag[4];
        uint32_t m_size;
        uint32_t m_flags;
        formid_t m_formid;
        uint32_t m_versnInfo1;
        uint16_t m_versnInfo2;
        uint16_t m_versnInfo3;
    };
    const TES4HdrOv BADTES4HDRREC = TES4HdrOv();

    struct TES4LongStringOv
    {
        TES4LongStringOv() { memset(this, 0, sizeof(*this)); }
        uint32_t m_size;
        char m_string; // start of string ends with '\0'
    };
    const TES4LongStringOv BADLONGSTRING = TES4LongStringOv();

    struct TES4CharBuffOv
    {
        TES4CharBuffOv() { memset(this, 0, sizeof(*this)); }
        uint32_t m_size;
        char m_string; // start of char buffer, does NOT terminate with '\0' size defines the length
    };
    const TES4CharBuffOv BADCHARBUFF = TES4CharBuffOv();

    struct HEDRHdrOv {
        HEDRHdrOv() { memset(this, 0, sizeof(*this)); }
        char m_HEDRtag[4];
        uint16_t m_size;
        uint32_t m_versn;
        uint32_t m_recordcount;
        formid_t m_nextobjectid;
    };
    const HEDRHdrOv BADHEDRHDRREC = HEDRHdrOv();

    struct MASTOv { // Name of the master file the ESP uses
        MASTOv() { memset(this, 0, sizeof(*this)); }
        char m_MASTtag[4];
        uint16_t m_size;
        uint8_t m_name; // first char in string ends with '\0'
    };
    const MASTOv BADMASTREC = MASTOv();

    // Common to multiple record types
    struct EDIDrecOv { // Used for the internal textual name
        EDIDrecOv() { memset(this, 0, sizeof(*this)); }
        char m_EDIDtag[4];
        uint16_t m_size;
        uint8_t m_name; // first char in string ends with '\0'
    };
    const EDIDrecOv BADEDIDREC = EDIDrecOv();

    struct BFCBrecOv { // Block of Components Begin
        BFCBrecOv() { memset(this, 0, sizeof(*this)); }
        char m_BFCBtag[4];
        uint16_t m_size;
        uint8_t m_name; // first char in string ends with '\0'
    };
    const BFCBrecOv BADBFCBREC = BFCBrecOv();

    struct BFCBDatarecOv { // Block of Components Begin
        BFCBDatarecOv() { memset(this, 0, sizeof(*this)); }
        char m_DATAtag[4];
        uint16_t m_size;
        uint8_t m_data; // first byte in array of data
    };
    const BFCBDatarecOv BADBFCBDATAREC = BFCBDatarecOv();

    struct BFCErecOv { // Block of Components End
        BFCErecOv() { memset(this, 0, sizeof(*this)); }
        char m_BFCBtag[4];
        uint16_t m_size; // Seems to be always 0.
    };
    const BFCErecOv BADBFCEREC = BFCErecOv();

    // Holds both the name and block of data for a component record
    // Not as stored in the file
    struct COMPRec {
        COMPRec(const BFCBrecOv* pBfcbName, const BFCBDatarecOv* pBfcbData) : m_pBfcbName(pBfcbName), m_pBfcbData(pBfcbData) {}
        const BFCBrecOv* m_pBfcbName;
        const BFCBDatarecOv* m_pBfcbData;
    };

    struct PPBDOv  // holds POI data for planet as na array of these
    {
        PPBDOv() { memset(this, 0, sizeof(*this)); }
        uint8_t m_PPBDtag[4];
        uint16_t m_size;
        uint8_t m_data; // first byte in a set m_size long
    };
    const PPBDOv BADPPBDDREC = PPBDOv();

    struct FULLrecOv { // Full user visible name
        FULLrecOv() { memset(this, 0, sizeof(*this)); }
        char m_FULLtag[4];
        uint16_t m_size;
        uint8_t m_name; // first byte in data, could be string or data
    };
    const FULLrecOv BADFULLREC = FULLrecOv();

    struct KSIZrecOv { // keys size - size of array to follow
        KSIZrecOv() { memset(this, 0, sizeof(*this)); }
        char m_KSIZtag[4];
        uint16_t m_size;
        uint32_t m_count;
    };
    const KSIZrecOv BADKSIZREC = KSIZrecOv();

    struct KWDArecOv { // Keyword reference
        KWDArecOv() { memset(this, 0, sizeof(*this)); }
        char m_KWDAtag[4];
        uint16_t m_size; // size of data
        uint8_t m_data; // first byte in data, this is an array of 4byte references
    };
    const KWDArecOv BADKWDAREC = KWDArecOv();

    // Group record which has set of other record types in it
    struct GRUPHdrOv {
        GRUPHdrOv() { memset(this, 0, sizeof(*this)); }
        char m_GRUPtag[4];
        uint32_t m_size;
    };
    const GRUPHdrOv BADGRUPHDRREC = GRUPHdrOv();

    struct GRUPBlankOv { // after GRUP there is a short blank record using the same tag as the types of records in it
        GRUPBlankOv() { memset(this, 0, sizeof(*this)); }
        uint8_t m_tag[4]; // depends type of records
        uint32_t m_size; // zero for these records
        uint32_t m_flags; // 0x3106 for these records
        formid_t m_formid; // zero for thee records
    };
    const GRUPBlankOv BADGRUPBLANKREC = GRUPBlankOv();

    struct STDTHdrOv {
        STDTHdrOv() { memset(this, 0, sizeof(*this)); }
        uint8_t m_STDTtag[4];
        uint32_t m_size;
        uint32_t m_flags;
        formid_t m_formid;
        uint32_t m_versnInfo1;
        uint16_t m_versnInfo2;
        uint16_t m_versnInfo3;
    };
    const STDTHdrOv BADSTDTHDRREC = STDTHdrOv();

    // TODO other records for STDT
    // There are ANAM, BNAM, CNAM ... GNAME records, each hold different types of data which is different in each record types
    // In otherwords, STDT BNAM is diffeent to a PNDT BNAM. Right now this is just finding and loading the minimal ones
    // needed to looked at or edited to create a copy of star or planet. the format is normal the 4 byte tag, a 2 byte size then the
    // data records which could be a string, or multiple other value types
    struct STDTAnamOv
    {
        STDTAnamOv() { memset(this, 0, sizeof(*this)); }
        uint8_t m_ANAMtag[4];
        uint16_t m_size;
        uint8_t m_aname; // first char in string ends with '\0'
    };
    const STDTAnamOv BADSTDTANAMREC = STDTAnamOv();

    struct STDTBnamOv
    {
        STDTBnamOv() { memset(this, 0, sizeof(*this)); }
        uint8_t m_BNAMtag[4];
        uint16_t m_size;
        float m_xPos;
        float m_yPos;
        float m_zPos;
    };
    const STDTBnamOv BADSTDTBNAMREC = STDTBnamOv();

    struct STDTDnamOv
    {
        STDTDnamOv() { memset(this, 0, sizeof(*this)); }
        uint8_t m_DNAMtag[4];
        uint16_t m_size;
        formid_t m_systemId;
    };
    const STDTDnamOv BADSTDTDNAMREC = STDTDnamOv();

    // Some of the componets in the component list that are of interest
    // TODO - need a general handler that can process the generic component data records and map them to specific ovs
    struct STDT_BGSStarDataComponent1Ov
    {
        STDT_BGSStarDataComponent1Ov() { memset(this, 0, sizeof(*this)); }
        char m_DATAtag[4];
        uint16_t m_size;
    };
    const STDT_BGSStarDataComponent1Ov BADBGSSTARDATACOMP1REC = STDT_BGSStarDataComponent1Ov();

    struct STDT_BGSStarDataComponent2Rec
    {
        const TES4CharBuffOv *m_pCatalogID;
        const TES4CharBuffOv *m_pSpectralClass;
    };

    struct STDT_BGSStarDataComponent3Ov
    {
        STDT_BGSStarDataComponent3Ov() { memset(this, 0, sizeof(*this)); }
        float m_Mass;
        float m_InnerHabzone;
        float m_OuterHabszone;
        uint32_t m_HIP;
        uint32_t m_Radius;
        uint32_t m_tempK;
        TES4LongStringOv m_oSpectralClass;
    };
    const STDT_BGSStarDataComponent3Ov BADBGSSTARDATACOMP3REC = STDT_BGSStarDataComponent3Ov();

    // Star - STDT record which pulls the above togather in one record which references the others build during loading or refeshed after mods
    struct STDTrec {
        bool m_isBad;
        bool m_isMissingMatchingBfce;
        const STDTHdrOv* m_pHdr;
        const EDIDrecOv* m_pEdid;
        std::vector<COMPRec>m_oComp; // set of components
        STDT_BGSStarDataComponent2Rec m_oBGSStarDataCompStrings;
        const STDT_BGSStarDataComponent3Ov* m_pBGSStarDataCompInfo;
        // TODO: other records
        const STDTAnamOv* m_pAnam;
        const STDTBnamOv* m_pBnam;
        const STDTDnamOv* m_pDnam;
        fPos getfPos() const { return m_pBnam ? fPos(m_pBnam->m_xPos, m_pBnam->m_yPos, m_pBnam->m_zPos) : fPos(0, 0, 0); }
    };

    // PNDT header record
    struct PNDTHdrOv
    {
        PNDTHdrOv() { memset(this, 0, sizeof(*this)); }
        uint8_t m_PNDTtag[4];
        uint32_t m_size;
        uint32_t m_flags;
        formid_t m_formid;
        uint32_t m_versnInfo1;
        uint16_t m_versnInfo2;
        uint16_t m_versnInfo3;
        uint32_t m_decompsize;
    };
    const PNDTHdrOv BADPNDTHDRREC = PNDTHdrOv();

    // TODO other records for PNDT
    struct PNDTAnamOv // TODO make this generic
    {
        PNDTAnamOv() { memset(this, 0, sizeof(*this)); }
        uint8_t m_ANAMtag[4];
        uint16_t m_size;
        uint8_t m_aname; // first char in string ends with '\0'
    };
    const PNDTAnamOv BADPNDTANAMREC = PNDTAnamOv();

    struct PNDTCnamOv
    {
        PNDTCnamOv() { memset(this, 0, sizeof(*this)); }
        uint8_t m_CNAMtag[4];
        uint16_t m_size; // seems to be just a marker record for a set of PPBD records that follow
    };
    const PNDTCnamOv BADPNDCANAMREC = PNDTCnamOv();

    struct PNDTGnamOv
    {
        PNDTGnamOv() { memset(this, 0, sizeof(*this)); }
        uint8_t m_GNAMtag[4];
        uint16_t m_size;
        formid_t m_systemId; // a unique id tied to the location which defines the star system
        uint32_t m_parentPndtLocalId; // the 1..n based sequence position of the parent planet if this is a moon, zero if not a moon
        uint32_t m_PndtLocalId; // the 1..n sequence positon of the planet e.g. 3rd rock from the sun, moons are in this sequence but after all the planets
    };
    const PNDTGnamOv BADPNDTGNAMREC = PNDTGnamOv();

    struct PNDTHnam1Ov // made up of 3 parts due to varible section in the middle of the PNDT HNAM record PNDTHnam1Ov-[PNDTHnam2Rec]-PNDTHnam3Ov
    {
        PNDTHnam1Ov() { memset(this, 0, sizeof(*this)); }
        uint8_t m_HNAMtag[4];
        uint16_t m_size;
        uint32_t m_notused1;
    };
    const PNDTHnam1Ov BADPNDTHNAMREC1 = PNDTHnam1Ov();

    #define NUMHNAMSTRINGS 8
    const std::string PNDTHnam2StringLabel[NUMHNAMSTRINGS] = { "SpectralClass", "CatalogueID", "Life", "Magnetoshere", "Mass(Kg)", "Type", "SettledStar", "Special" };

    struct PNDTHnam2Rec // Not a OV: Varible lenght structure of strings in the middle of PNDT HNAM
    {
        PNDTHnam2Rec() { memset(this, 0, sizeof(*this)); }
        const TES4LongStringOv *m_Strings[NUMHNAMSTRINGS];
    };
    struct PNDTHnam3Ov
    {
        PNDTHnam3Ov() { memset(this, 0, sizeof(*this)); }
        double m_perihelion;
        double m_stardist;
        float m_density;
        float m_heat;
        float m_Hydro;
        float m_innerhz;
        float m_outerhz;
        float m_periangle;
        uint32_t unsed2;
        float m_startangle;
        float m_yearlen;
        uint32_t m_asteriods;
        uint32_t m_geostationary;
        uint32_t m_randomseed;
        uint32_t m_rings;
    };
    const PNDTHnam3Ov BADPNDTHNAMREC3 = PNDTHnam3Ov();

    // Planet - PNDT record which pulls the above togather in one record which references the others build during loading or refeshed after mods
    // Rec is different in that the pointers after the m_decompress point into the decompress buffer while the compressed buffer and header
    // point into the original m_buffer data.
    struct PNDTrec
    {
        bool m_isBad;
        bool m_isMissingMatchingBfce;
        const PNDTHdrOv* m_pHdr;
        const char* m_pcompdata; // compressed data
        uint32_t m_compdatasize;
        std::vector<char> m_decompdata;
        const EDIDrecOv* m_pEdid;
        std::vector<COMPRec>m_oComp; // set of components
        // TODO: other records
        const PNDTAnamOv* m_pAnam;
        const PNDTCnamOv* m_pCnam;
        std::vector<const PPBDOv*> m_oPpbds; // Set of PPBD records which old biom information
        const PNDTGnamOv* m_pGnam;
        const PNDTHnam1Ov* m_pHnam1;
        PNDTHnam2Rec m_oHnam2;
        const PNDTHnam3Ov* m_pHnam3;
    };

    // LCTN header record
    struct LCTNHdrOv
    {
        LCTNHdrOv() { memset(this, 0, sizeof(*this)); }
        uint8_t m_LCTNtag[4];
        uint32_t m_size; // size of full record excluding size of hdr
        uint32_t m_flags;
        formid_t m_formid;
        uint32_t m_versnInfo1;
        uint16_t m_versnInfo2;
        uint16_t m_versnInfo3;
    };
    const LCTNHdrOv BADLCTNHDRREC = LCTNHdrOv();

    struct LCTNDataOv
    {
        LCTNDataOv() { memset(this, 0, sizeof(*this)); }
        uint8_t m_DATAtag[4];
        uint16_t m_size; 
        formid_t m_faction;
        uint8_t m_val1;
        uint8_t m_playerLvl;
        uint8_t m_val2;
        uint8_t m_playerLvlMax;
    };
    const LCTNDataOv BADLCTNDATAREC = LCTNDataOv();

    struct LCTNAnamOv // TODO make this generic
    {
        LCTNAnamOv() { memset(this, 0, sizeof(*this)); }
        uint8_t m_ANAMtag[4];
        uint16_t m_size;
        uint8_t m_aname; // first char in string ends with '\0'
    };
    const LCTNAnamOv BADLCTNANAMREC = LCTNAnamOv();

    struct LCTNPnamOv // TODO make this generic
    {
        LCTNPnamOv() { memset(this, 0, sizeof(*this)); }
        uint8_t m_PNAMtag[4];
        uint16_t m_size;
        formid_t m_parentloc; // first char in string ends with '\0'
    };
    const LCTNPnamOv BADLCTNPNAMREC = LCTNPnamOv();

    struct LCTNXnamOv
    {
        LCTNXnamOv() { memset(this, 0, sizeof(*this)); }
        uint8_t m_XNAMtag[4];
        uint16_t m_size;
        formid_t m_systemId;
    };
    const LCTNXnamOv BADLCTNXNAMREC = LCTNXnamOv();

    struct LCTNYnamOv
    {
        LCTNYnamOv() { memset(this, 0, sizeof(*this)); }
        uint8_t m_YNAMtag[4];
        uint16_t m_size;
        uint32_t m_planetpos;
    };
    const LCTNYnamOv BADLCTNYNAMREC = LCTNYnamOv();

    // Location - LCTN record which pulls the above togather in one record which references the others build during loading or refeshed after mods
    struct LCTNrec
    {
        bool m_isBad;
        const LCTNHdrOv* m_pHdr;
        const EDIDrecOv* m_pEdid;
        const FULLrecOv* m_pFull;
        const KSIZrecOv* m_pKsiz;
        const KWDArecOv* m_pKwda;
        // TODO: other records
        const LCTNDataOv* m_pData;
        const LCTNAnamOv* m_pAnam;
        const LCTNPnamOv* m_pPnam;
        const LCTNXnamOv* m_pXnam;
    };
#pragma pack(pop)

    // General record to point to any record in one of collections
    struct GENrec
    {
        GENrec() : m_eType(eESP_IDK), m_iIdx(0) {};
        GENrec(ESPRECTYPE eType, size_t idx) : m_eType(eType), m_iIdx(idx) {}
        ESPRECTYPE m_eType;
        size_t m_iIdx; // index in collection decided by type
    };

    // GRUPs are blocks of records normally containing the same type of records and one GRUP per type
    // But using a vector of them in case more than one is found for a specific type
    // Note assumes if GRUP starts with a tag then it only contains records of that type indicated by the tag
    std::vector<const GRUPHdrOv *> m_grupStdtPtrs; // pointers STDT GRUP records 
    std::vector<const GRUPHdrOv *> m_grupPndtPtrs; // pointers PNDT GRUP records
    std::vector<const GRUPHdrOv *> m_grupLctnPtrs; // pointers LCTN GRUP records

    // file read data, records structs will point into this
    std::vector<char> m_buffer;

    // Collections of STDT records and PNDT records
    std::vector<STDTrec> m_stdts;
    std::vector<PNDTrec> m_pndts;
    std::vector<LCTNrec> m_lctns;

    // Indexes
    std::unordered_map<formid_t, GENrec> m_FmIDMap; // formid to record map
    std::unordered_map<formid_t, std::vector<GENrec>> m_SystemIDMap; // star system id to things in that system

    std::mutex m_output_mutex; // used for multi-treading
    std::wstring m_wstrfilename; // Loaded file to be used as source of data
    std::string m_strMasterFile; // loaded from ESP or ESM file header
    bool m_bIsSaved; // false if there are outstanding changes to save

    // Bad record stubs for overlays and tracking
    // This means if the data is missing or bad we don't end up pointing into random memory
    std::unordered_map<formid_t, GENrec> m_BadMap; // track found bad records 
    std::unordered_map<formid_t, GENrec> m_MissingBfceMap; // track records where there is a block start but no block end.

    // First records in file, no need to process set the pointers
    const TES4HdrOv *getTES4Hdr_ptr() 
    { 
        if (m_buffer.size() < sizeof(TES4HdrOv))
            return &BADTES4HDRREC;
        return reinterpret_cast<TES4HdrOv*>(&m_buffer[0]); 
    }
    const HEDRHdrOv* getHEDRHdr_ptr()
    {
        if (m_buffer.size() < sizeof(TES4HdrOv) + sizeof(HEDRHdrOv))
            return &BADHEDRHDRREC;
        return reinterpret_cast<HEDRHdrOv*>(&m_buffer[sizeof(TES4HdrOv)]);
    }

public:
    
    bool isSaved() const                { return m_bIsSaved; }
    std::wstring getFname() const       { return m_wstrfilename; }
    std::string getMasterFname() const  { return m_strMasterFile; } // The name of the master file the ESP uses. For ESMs this will be empty
    bool isESM() const                  { return m_strMasterFile.empty(); }
    size_t getMissingBfceCount() const  { return m_MissingBfceMap.size(); }


    std::string cbToStr(const TES4CharBuffOv* pCharBuff)
    {
        std::string strOut;
        if (pCharBuff && pCharBuff->m_size)
            for (uint32_t i = 0; i < pCharBuff->m_size; i++)
                strOut += (&(pCharBuff->m_string))[i];
        return strOut;
    }

    void setNewFname(const std::wstring &wstrNewFileName) 
    { 
        m_wstrfilename = wstrNewFileName; 
    }

    std::string getFnameAsStr(const std::wstring &wstrfname)
    {
        std::filesystem::path filePath(wstrfname);
        return filePath.string();
    }

    std::string getFnameAsStr()
    {
        std::filesystem::path filePath(m_wstrfilename);
        return filePath.string();
    }

    std::string getFnameRoot()
    {
        std::filesystem::path filePath(m_wstrfilename);
        return filePath.filename().string();
    }

    std::string getAnam(ESPRECTYPE eType, size_t iIdx)
    {
        switch (eType) 
        {
            case eESP_STDT: 
                if (iIdx < m_stdts.size())
                    return std::string(reinterpret_cast<const char*>(&m_stdts[iIdx].m_pAnam->m_aname));
                break;

            case eESP_PNDT:
                if (iIdx < m_pndts.size())
                    return std::string(reinterpret_cast<const char*>(&m_pndts[iIdx].m_pAnam->m_aname));
                break;
            case eESP_LCTN:
                if (iIdx < m_lctns.size())
                    return std::string(reinterpret_cast<const char*>(&m_lctns[iIdx].m_pAnam->m_aname));
                break;
        }

        return "";
    }

    size_t getNum(ESPRECTYPE eType)
    {
        switch (eType) {
        case eESP_STDT: return m_stdts.size();
        case eESP_PNDT: return m_pndts.size();
        case eESP_LCTN: return m_lctns.size();
        }
        return 0;
    }

    size_t getGrupNum(ESPRECTYPE eType)
    {
        switch (eType) {
        case eESP_STDT: return m_grupStdtPtrs.size();
        case eESP_PNDT: return m_grupPndtPtrs.size();
        case eESP_LCTN: return m_grupLctnPtrs.size();
        }
        return 0;
    }

    // for swaping position to get different views of star map
    enum POSSWAP { PSWAP_NONE, PSWAP_XZ, PSWAP_XY, PSWAP_YZ, PSWAP_XFLIP, PSWAP_YFLIP, PSWAP_ZFLIP };


private: 
    template <typename T> T* makeMutable(const T* ptr);

    // for loading
    bool compress_data(const char* input_data, size_t input_size, std::vector<char>& compressed_data);
    bool decompress_data(const char* compressed_data, size_t compressed_size, std::vector<char>& decompressed_data, size_t decompressed_size);
    ESPRECTYPE getRecType(const char* ptag);
    bool findFmIDMap(formid_t formid, GENrec& fndrec);
    bool findKeyword(const LCTNrec& oRec, uint32_t iKeyword);
    bool findLocInfo(const STDTrec& oRecStar, size_t& iPlayerLvl, size_t& iPlayerLvlMax, size_t &iFaction);
    size_t findPrimaryIdx(size_t iIdx);
    size_t findPrimaryIdx(size_t iIdx, fPos& oSystemPosition);
    size_t findPndtsFromStdt(size_t iIdx, std::vector<size_t>& oFndPndts);
    bool isBadPosition(const fPos& oPos);
    void _readCharBuff(const TES4CharBuffOv*& pCharBuff, const char*& searchPtr, const char*& endPtr);
    void _readLongString(const TES4LongStringOv*& pString, const char*& searchPtr, const char*& endPtr);
    bool _readBFCBtoBFBE(const char*& searchPtr, const char*& endPtr, std::vector<COMPRec>& oComp);
    void _doBfcbquickskip(const char*& searchPtr, const char*& endPtr);
    void _dobuildsubrecs_mt(const std::vector<const GRUPHdrOv*>& vgrps, const char* searchPatt, ESPRECTYPE eType);
    void do_process_subrecs_mt();
    size_t getmtclunks(size_t num_pointers, size_t& num_threads);

    // planets
    BasicInfoRec _makeBasicPlanetRec(const size_t iIdx);
    void _buildppbdlist(PNDTrec& oRec, const char*& searchPtr, const char*& endPtr);
    void _buildHnamRec(PNDTHnam2Rec& oHnam2, const char*& searchPtr, const char*& endPtr);
    void _dopndt_op_findparts(PNDTrec& oRec, const char*& searchPtr, const char*& endPtr);
    void _dopndt_op(size_t iPndtIdx);
    void process_pndt_ranged_op_mt(size_t start, size_t end);
    void dopndt_op_mt();

    // stars
    void _extractCompOfInterest(STDTrec& oRec, const char* &endPtr);
    BasicInfoRec _makeBasicStarRec(const size_t iIdx);
    void _dostdt_op_findparts(STDTrec& oRec, const char*& searchPtr, const char*& endPtr);
    void process_stdt_ranged_op_mt(size_t start, size_t end);
    void _dostdt_op(size_t iStdtIdx);
    void dostdt_op_mt();

    // Locations
    BasicInfoRec _makeBasicLocRec(const size_t iIdx);
    void _dolctn_op_findparts(LCTNrec& oRec, const char*& searchPtr, const char*& endPtr);
    void _dolctn_op(size_t iIdx);
    void process_lctn_ranged_op_mt(size_t start, size_t end);
    void dolctn_op_mt();
    void do_process_lctns();

    bool loadfile(std::string& strErr);
    bool loadhdrs();
    bool _loadfrombuffer(std::string& strErr);

    // debuging and info dump
    void _debugDumpVector(const std::vector<char>& oV, std::string strNamepostfix);
    std::string _dumpComps(const std::vector<COMPRec>& oComps);
    std::string _dumpKeywords(const CEsp::KSIZrecOv* pKsiz, const KWDArecOv* pKwda);
    std::string _dumpStdt(const STDTrec& oRec);
    std::string _dumpPndt(const PNDTrec& oRec);
    std::string _dumpLctn(const LCTNrec& oRec);
    void _dumpToFile(const std::vector<std::string>& oOutputs, const std::string& fileName, bool bAppend);
    size_t dumpMissingBfceMapRecs(std::vector<std::string>& oOutputs);

    // For general writing data
    formid_t _createNewFormId();
    formid_t _createNewSystemId();
    uint32_t _incNumObjects();
    GenBlock* _makegenblock(const char *tag, const void* pdata, size_t ilen);
    GenBlock* _makegenblock(const char* tag, const char* pdata);
    void _deleterec(std::vector<char>& newbuff, const char* removestart, const char* removeend);
    void _addtobuff(std::vector<char>& buffer, void* pdata, size_t datasize);
    void _insertbuff(std::vector<char>& newbuff, char* pDstInsertPosition, size_t oldSize, const char* pNewbuff, size_t iSizeNewBuffer);
    void _insertname(std::vector<char>& newbuff, char* pDstName, uint16_t* pNameSize, const char* pNewName);
    bool appendToGrup(char *pgrup, const std::vector<char>& insertData);
    bool createGrup(const char *pTag, const std::vector<char> &newBuff);
    formid_t _createLoc(std::vector<char> &newLocbuff, const char* szLocName, const char* szOrbName, formid_t parentid, formid_t systemid, uint32_t *pkeywords, size_t numkeywords, size_t faction, size_t iLvlMin, size_t iLvlMax, size_t iPlanetPosNum);

    // Star
    bool _refreshsizeStdt(std::vector<char>& newbuff, const STDTrec& oRec);
    void _rebuildStdtRecFromBuffer(STDTrec &oRec, const std::vector<char>& newstdbuff);
    void _clonefixupcompsStdt(std::vector<char>& newbuff, STDTrec& oRec);
    formid_t createLocStar(const std::vector<char> &newStarbuff, const BasicInfoRec &oBasicInfo, std::vector<char> &newLocbuff);
    bool cloneStdt(std::vector<char> &newbuff, const STDTrec &ostdtRec, const BasicInfoRec &oBasicInfo);

    // Planet
    size_t _updateCompressed(std::vector<char> &buff, PNDTrec& oRec);
    bool _adjustPlanetLocalIds(const size_t iPrimaryIdx, const size_t iNewPlanetIdx, const size_t iPlanetPos);
    void _decompressPndt(PNDTrec& oRec, const std::vector<char>& newpndbuff);
    bool _refreshHdrSizesPndt(const PNDTrec& oRec, size_t decomsize);
    void _rebuildPndtRecFromDecompBuffer(PNDTrec& oRec);
    void _clonefixupcompsPndt(PNDTrec& oRec);
    bool createLocPlanet(const std::vector<char> &newPlanetbuff, const BasicInfoRec &oBasicInfo, std::vector<char> &newLocbuff);
    formid_t clonePndt(std::vector<char>& newbuff, const PNDTrec& opndtRec, const BasicInfoRec& oBasicInfo);
    
    // main modifications
    bool _saveToFile(const std::vector<char>& newbuff, const std::wstring& wstrfilename, std::string& strErr);

public:
    // UX to CEsp data sharing
    bool getBasicInfo(ESPRECTYPE eType, size_t iIdx, BasicInfoRec& oBasicInfoRec);
    bool getBasicInfo(ESPRECTYPE eType, formid_t formid, BasicInfoRec& oBasicInfoRec);
    bool getBasicInfo(formid_t iSystemId, size_t iParentPlanetID, BasicInfoRec& oBasicInfoRec);
    void getBasicInfoRecs(CEsp::ESPRECTYPE eType, std::vector<BasicInfoRec>& oBasicInfos, bool bExcludeblanks = false);

    // debugging and bad data
    std::string dumpStats();
    void dumptofile(const std::string& fileName);
    size_t dumpBadRecs(std::vector<std::string>& oOutputs);
    bool checkformissingbiom(const std::wstring& wstrSrcFilePath, std::vector<std::string>& strErrs);
    void dumpPlanetPositions(size_t iStarIdx, std::string& strOut);


    // for maps
    void getPlanetPerihelion(size_t iStarIdx, std::vector<PlanetPlotData>& oPlanetPlots, double& min, double& max);
    float calcDist(const fPos& p1, const fPos& p2);
    void getMoons(size_t iPlanetIdx, std::vector<BasicInfoRec>& oBasicInfos);
    void getBasicInfoRecsOrbitingPrimary(ESPRECTYPE eType, size_t iPrimary, std::vector<BasicInfoRec>& oBasicInfos, bool bIncludeMoons, bool bIncludeUnlandable);
    float findClosestDist(const size_t iSelfIdx, const fPos &targetPos, const std::vector<BasicInfoRec>& oBasicInfoRecs, size_t& idx);
    float getMinDistance(float fMinDistance = std::numeric_limits<float>::max());
    bool checkMinDistance(const fPos& ofPos, float fMinDistance, std::string &strErr);
    fPos findCentre();
    fPos posSwap(const fPos& oOrgPos, POSSWAP eSwapXZ);
    void getStarPositons(std::vector<StarPlotData>& oStarPlots, fPos& min, fPos& max, POSSWAP eSwap);

    // main operations
    bool makestar(const CEsp *pSrc, size_t iSrcStarIdx, const BasicInfoRec &oBasicInfo, std::string &strErr);
    bool makeplanet(const CEsp* pSrc, size_t iSrcPlanetIdx, const BasicInfoRec& oBasicInfo, formid_t &FormIdforNewPlanet, std::string& strErr);
    bool makebiomfile(const std::wstring& wstrSrcFilePath, const std::string& strSrcPlanetName, const std::string& strDstName, std::wstring &wstrNewFileName, std::string& strErr);
    bool copyToBak(std::string &strBakUpName, std::string& strErr);
    bool checkdata(std::string& strErr);
    bool save(std::string &strErr);
    bool load(const std::wstring& strFileName, std::string& strErr);
};
