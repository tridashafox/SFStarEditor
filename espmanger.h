// espmanger.h: processing of ESM and ESP files
//
#define NOMINMAX
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
#include <zlib.h> 
#include <limits> 
#include <thread>
#include <mutex>
#include <future>
#include <algorithm>
#include <unordered_set>
#include <cmath>
#include <chrono>

#pragma comment(lib, "Shcore.lib")
extern const char* SZBADRECORD;

// Class for processing a .ESP/ESM file
// TODO: Loops need to be decomposing header records and offsets, rather than blindly looking for the 
// four letter tag. For example a name with the text ANAM in it will cause an issue
class CEsp
{
public:
    CEsp() { m_bIsSaved = true;  };
    ~CEsp() {};

    enum ESPRECTYPE { eESP_IDK, eESP_PNDT, eESP_STDT, eESP_LCTN };

    #define NO_ORBIT -1 
    #define NO_RECIDX -1
    #define GENBUFFSIZE (1024)
    #define STARMAPMAX (30.0) // bounder of starmap planet positions should be within this
    #define ESP_FORMIDMASK (0x01FFFFFF) // mask to just affect the top byte of a form id
    #define ESP_FORMIDPREF (0x01000000) // looks like this should be 0x01 for ESPs
    #define MAXKEYWORDS (255) // There should not be more than 255 keywords in list (protection against bad data)
    #define MAXCOMPINREC (3000) // Should not be more than this many components some objects have 1000s of this
    #define LIMITCOMPSTO (20) // Currently limiting how many we load to 10, since we only intereted in a few of these.
    #define MAXCOMPFORDUMPING 3 // used for to limit dump output
    #define BLEFT ((size_t)(endPtr - searchPtr))
    #define BSKIP(x) (sizeof(x->m_size)+x->m_size+4)
    #define MKFAIL(x) { strErr = x; return false; }

    using formid_t = uint32_t;

    // standrd objects needed for creating data
    const uint32_t KW_LocTypeStarSystem = 0x149F;
    const formid_t FID_Universe = 0x1A53A;

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
            const bool bMoon, const fPos& oPos, const size_t iIdx, const size_t iPrimaryIdx = NO_ORBIT, const size_t iPlanetPos = 0, 
            const size_t iSysPlayerLvl = 70, const size_t iSysPlayerLvlMax = 255, const size_t iFaction = 0) :
            m_eType(eType), m_pName(pName), m_pAName(pAName), m_bIsMoon(bMoon), m_StarMapPostion(oPos),
            m_iIdx(iIdx), m_iPrimaryIdx(iPrimaryIdx), m_iPlanetPos(iPlanetPos), m_iSysPlayerLvl(iSysPlayerLvl), m_iSysPlayerLvlMax(iSysPlayerLvlMax), 
            m_iFaction(iFaction) {}
        void clear()
        {
            m_pName = m_pAName = SZBADRECORD;
            m_bIsMoon = false;
            m_StarMapPostion.clear();
            m_eType = eESP_IDK;
            m_iIdx = NO_RECIDX;
            m_iPrimaryIdx = NO_ORBIT;
            m_iPlanetPos = 0;
            m_iSysPlayerLvl = 0;
            m_iSysPlayerLvlMax = 0;
            m_iFaction = 0;
        }

        const char* m_pName;   // Form name
        const char* m_pAName;  // Presentation name
        bool m_bIsMoon;        // If the object is a moon
        fPos m_StarMapPostion; // Position on the star map
        ESPRECTYPE m_eType;    // Type of object
        size_t m_iIdx;         // Idx to core data
        size_t m_iPrimaryIdx;  // Idx to parent star [TODO: Or planet, in which case it will need a eType for the PrimaryIdx]
        size_t m_iPlanetPos;   // The position for a planet [TODO: how are moons handled]
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

private:
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

    void endTC(const std::string &pref, timept startTime)
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
    const TES4HdrOv  BADTES4HDRREC  = TES4HdrOv();

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
        uint8_t m_name; // first byte in array of data
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

    // Star - STDT record which pulls the above togather in one record which references the others build during loading or refeshed after mods
    struct STDTrec {
        bool m_isBad;
        bool m_isMissingMatchingBfce;
        const STDTHdrOv* m_pHdr;
        const EDIDrecOv* m_pEdid;
        std::vector<COMPRec>m_oComp; // set of components
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

    struct PNDTGnamOv
    {
        PNDTGnamOv() { memset(this, 0, sizeof(*this)); }
        uint8_t m_GNAMtag[4];
        uint16_t m_size;
        formid_t m_systemId;
        uint32_t m_primePndtId;
        uint32_t m_pndtId;
    };
    const PNDTGnamOv BADPNDTGNAMREC = PNDTGnamOv();

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
        const PNDTGnamOv* m_pGnam;
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
        uint8_t m_DNAMtag[4];
        uint16_t m_size;
        formid_t m_systemId;
    };
    const LCTNXnamOv BADLCTNXNAMREC = LCTNXnamOv();

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
    
    bool isSaved()                  { return m_bIsSaved; }
    std::wstring getFname()         { return m_wstrfilename; }
    std::string getMasterFname()    { return m_strMasterFile; } // The name of the master file the ESP uses. For ESMs this will be empty
    bool isESM()                    { return m_strMasterFile.empty(); }
    size_t getMissingBfceCount()    { return m_MissingBfceMap.size(); }

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
    // for loading
    bool compress_data(const char* input_data, size_t input_size, std::vector<char>& compressed_data);
    bool decompress_data(const char* compressed_data, size_t compressed_size, std::vector<char>& decompressed_data, size_t decompressed_size);
    ESPRECTYPE getRecType(const char* ptag);
    bool findFmIDMap(formid_t formid, GENrec& fndrec);
    void mergeFmIDMaps(std::unordered_map<formid_t, GENrec>& targetMap, const std::unordered_map<formid_t, GENrec>& sourceMap);
    bool findKeyword(const LCTNrec& oRec, uint32_t iKeyword);
    bool findPlayerLvl(const STDTrec& oRecStar, size_t& iPlayerLvl, size_t& iPlayerLvlMax);
    size_t findPrimaryIdx(size_t iIdx, fPos& oSystemPosition);
    size_t findPndtsFromStdt(size_t iIdx, std::vector<size_t>& oFndPndts);
    bool isBadPosition(const fPos& oPos);
    void _doBfcbquickskip(const char*& searchPtr, const char*& endPtr);
    void _dobuildsubrecs_mt(const std::vector<const GRUPHdrOv*>& vgrps, const char* searchPatt, ESPRECTYPE eType);
    void do_process_subrecs_mt();
    size_t getmtclunks(size_t num_pointers, size_t& num_threads);

    // planets
    void _dopndt_op_findparts(PNDTrec& oRec, const char*& searchPtr, const char*& endPtr);
    void _dopndt_op(size_t iPndtIdx);
    void process_pndt_ranged_op_mt(size_t start, size_t end);
    void dopndt_op_mt();

    // stars
    void _dostdt_op_findparts(STDTrec& oRec, const char*& searchPtr, const char*& endPtr);
    void process_stdt_ranged_op_mt(size_t start, size_t end);
    void _dostdt_op(size_t iStdtIdx);
    void dostdt_op_mt();

    // Locations
    void _dolctn_op_findparts(LCTNrec& oRec, const char*& searchPtr, const char*& endPtr);
    void _dolctn_op(size_t iIdx);
    void process_lctn_ranged_op_mt(size_t start, size_t end);
    void dolctn_op_mt();
    void do_process_lctns();

    bool loadfile(std::string& strErr);
    bool loadhdrs();
    bool _loadfrombuffer(std::string& strErr);

    // debuging and info dump
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
    void _addtobuff(std::vector<char>& buffer, void* pdata, size_t datasize);
    void _insertbuff(std::vector<char>& newbuff, char* pDstName, uint16_t* pSizeToFixup, const char* pNewbuff, size_t iSizeNewBuffer);
    void _insertname(std::vector<char>& newbuff, char* pDstName, uint16_t* pNameSize, const char* pNewName);
    bool appendToGrup(char *pgrup, const std::vector<char>& insertData);
    bool createGrup(const char *pTag, const std::vector<char> &newBuff);

    // Star
    bool _refreshsizeStdt(std::vector<char>& newbuff, const STDTrec& oRec);
    void _rebuildStdtRecFromBuffer(STDTrec &oRec, const std::vector<char>& newstdbuff);
    void _clonefixupcompsStdt(std::vector<char>& newbuff, STDTrec& oRec);
    bool createLocStar(const std::vector<char> &newStarbuff, const BasicInfoRec &oBasicInfo, std::vector<char> &newLocbuff);
    bool cloneStdt(std::vector<char> &newbuff, const STDTrec &ostdtRec, const BasicInfoRec &oBasicInfo);

    // Planet
    size_t _adjustPlanetPositions(const STDTrec& ostdtRec, size_t iPlanetPos);
    bool _refreshsizePndt(std::vector<char>& newbuff, const PNDTrec& oRec);
    void _rebuildPndtRecFromBuffer(PNDTrec& oRec, const std::vector<char>& newpndtbuff, bool bDecomp);
    bool createLocPlanet(const std::vector<char> &newPlanetbuff, const BasicInfoRec &oBasicInfo, std::vector<char> &newLocbuff);
    bool clonePndt(std::vector<char>& newbuff, const PNDTrec& opndtRec, const BasicInfoRec& oBasicInfo);
    
    // main modifications
    bool _saveToFile(const std::vector<char>& newbuff, const std::wstring& wstrfilename, std::string& strErr);

public:
    // UX to CEsp data sharing
    bool getBasicInfo(ESPRECTYPE eType, size_t iIdx, BasicInfoRec& oBasicInfoRec);
    void getBasicInfoRecs(CEsp::ESPRECTYPE eType, std::vector<BasicInfoRec>& oBasicInfos);

    // debugging and bad data
    std::string dumpStats();
    void dumptofile(const std::string& fileName);
    size_t dumpBadRecs(std::vector<std::string>& oOutputs);

    // for star map
    float calcDist(const fPos& p1, const fPos& p2);
    void getBasicInfoRecsOrbitingPrimary(CEsp::ESPRECTYPE eType, formid_t iPrimary, std::vector<CEsp::BasicInfoRec>& oBasicInfos, bool bIncludeMoons);
    float findClosestDist(const size_t iSelfIdx, const fPos &targetPos, const std::vector<BasicInfoRec>& oBasicInfoRecs, size_t& idx);
    float getMinDistance(float fMinDistance = std::numeric_limits<float>::max());
    bool checkMinDistance(const fPos& ofPos, float fMinDistance, std::string &strErr);
    fPos findCentre();
    fPos posSwap(const fPos& oOrgPos, POSSWAP eSwapXZ);
    void getStarPositons(std::vector<StarPlotData>& oStarPlots, fPos& min, fPos& max, POSSWAP eSwap);

    // main operations
    bool makestar(const CEsp *pSrc, const BasicInfoRec &oBasicInfo, std::string &strErr);
    bool makeplanet(const CEsp* pSrc, const BasicInfoRec& oBasicInfo, std::string& strErr);
    bool copyToBak(std::string &strBakUpName, std::string& strErr);
    bool checkdata(std::string& strErr);
    bool save(std::string &strErr);
    bool load(const std::wstring& strFileName, std::string& strErr);
};
