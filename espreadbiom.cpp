#include "espmanger.h"
#include <zlib.h>
#include <locale>
#include <codecvt>

// get and extracted the required biom content for a planet
class CArc
{
public:
    CArc() { }

private:
#pragma pack(push, 2) // force byte alignment
	struct ARCHdr
	{
		char m_arcTag[4];
		uint32_t m_vern;
		char m_GrnlTag[4];
		uint32_t m_nfiles;
		uint64_t m_namePos;
        uint64_t unused;
        ARCHdr() { memset(this, 0, sizeof(ARCHdr)); }
	};

	struct FNHdr
	{
		uint32_t m_hash1;
		char ext[4];
		uint32_t m_hash2;
		uint32_t m_unused1;
		uint64_t m_offset;
		uint32_t m_compsize;
		uint32_t m_decompsize;
		uint32_t m_unused2;
        FNHdr() { memset(this, 0, sizeof(FNHdr)); }
	};

#pragma pack(pop)

	std::vector<FNHdr> m_fnhdrs;
	std::vector<std::string> m_names;
    std::vector<char> m_buffer;
    ARCHdr m_oHdr;
    
    bool _loadHdrs()
    {
        const size_t taglen = 4;
        size_t searchPos = 0;
        memcpy(&m_oHdr, &m_buffer[0], sizeof(ARCHdr));
        searchPos += sizeof(ARCHdr);

        if (memcmp(m_oHdr.m_arcTag, "BTDX", taglen) != 0)
            return false;

        m_fnhdrs.clear();
        if (memcmp(m_oHdr.m_GrnlTag, "GNRL", taglen) == 0)
        {
            for (size_t i = 0; i < m_oHdr.m_nfiles && searchPos + sizeof(FNHdr)<= m_buffer.size(); i++)
            {
                FNHdr oFnhdr;
                memcpy(&oFnhdr, &m_buffer[searchPos], sizeof(FNHdr));
                m_fnhdrs.push_back(oFnhdr);
                searchPos += sizeof(FNHdr);
            }
        }
        else
            return false;

        // Get names
        if (m_oHdr.m_namePos >= m_buffer.size())
            return false;

        searchPos = m_oHdr.m_namePos;

        // Read file names
        for (size_t i = 0; i < m_oHdr.m_nfiles; i++)
        {
            if (searchPos + sizeof(uint16_t) > m_buffer.size())
                return false; // Out of bounds check

            uint16_t len;
            memcpy(&len, &m_buffer[searchPos], sizeof(uint16_t));
            searchPos += sizeof(uint16_t);

            if (searchPos + len > m_buffer.size()) 
                return false;

            std::vector<char> filename(len + 1);
            memcpy(&filename[0], &m_buffer[searchPos], len);
            searchPos += len;
            filename[len] = '\0';
            std::string strFn = &filename[0];
            m_names.push_back(strFn);
        }
        return true;
    }

    std::string toLowerCase(const std::string& str) 
    {
        std::string lowerStr = str;
        std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), [](unsigned char c) { return std::tolower(c); });
        return lowerStr;
    }

public:

    bool loadfile(const std::wstring wstrfilename, std::string& strErr)
    {
        m_buffer.clear();
   
        std::string strFn;
        std::filesystem::path filePath(wstrfilename);
        strFn = filePath.string();

        std::ifstream file(wstrfilename, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            strErr = std::string("Could not open archive to retrieve biom ") + strFn;
            return false;
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        m_buffer.resize(size);
        if (!file.read(m_buffer.data(), size))
        {
            file.close();
            strErr = std::string("Unable to read data from ") + strFn;
            return false;
        }

        file.close();

        return _loadHdrs();
    }

    bool extract(const std::string &strSrcName, std::vector<char>& outbuff, std::string &strErr)
    {
        // find the file rec which matches the passed name
        std::string strTofind = toLowerCase(strSrcName);

        uint32_t fid;
        for (fid = 0; fid<m_names.size(); fid++)
            if (m_names[fid] == strTofind)
                break;

        if (fid >= m_fnhdrs.size())
        {
            strErr = "file not found in archive";
            return false;
        }

        // set up the out buff
        bool bComp = m_fnhdrs[fid].m_compsize > 0;
        std::vector<uint8_t>decompbuff;
        decompbuff.resize(m_fnhdrs[fid].m_decompsize);
        outbuff.resize(m_fnhdrs[fid].m_decompsize);

        size_t searchPtr = m_fnhdrs[fid].m_offset;
        if (bComp) 
        {
            if (searchPtr + m_fnhdrs[fid].m_compsize > m_buffer.size())
            {
                strErr = "Ran passed end of archive";
                return false;
            }

            std::vector<uint8_t> comp(m_fnhdrs[fid].m_compsize);
            std::copy(m_buffer.begin() + searchPtr, m_buffer.begin() + searchPtr + m_fnhdrs[fid].m_compsize, comp.begin());

            // do the decompress
            uLongf destLen = static_cast<uLongf>(outbuff.size());
            uLong lsize = static_cast<uLong>(comp.size());
            if (uncompress(decompbuff.data(), &destLen, comp.data(), lsize) != Z_OK)
            {
                strErr = "Failed to decompress data in archive";
                return false;
            }

            std::copy(decompbuff.begin(), decompbuff.end(), outbuff.begin());
        } 
        else 
        {
            if (searchPtr + outbuff.size() > m_buffer.size())
            {
                strErr = "Ran passed end of archive";
                return false;
            }

            std::copy(m_buffer.begin() + searchPtr, m_buffer.begin() + searchPtr + outbuff.size(), outbuff.begin());
        }

        return true;
    }
};

bool CEsp::makebiomfile(const std::wstring &wstrSrcFilePath, const std::string &strSrcPlanetName, const std::string &strDstName, std::wstring &wstrNewFileName, std::string& strErr)
{
	// Find the arhive file which contains the biom needed use pSrc to help find it
    CArc oArc;
    strErr.clear();

    std::filesystem::path directory(wstrSrcFilePath);
    directory /= L"Starfield - PlanetData.ba2";
    std::wstring wstrbiomArchiveFileName = directory.wstring();

    if (!oArc.loadfile(wstrbiomArchiveFileName, strErr))
        return false;

    std::vector<char> outbuff;
    const std::string strnestedpath = "planetdata/biomemaps/";
    std::string strSrcPlanetNameWithPathAndExt = strnestedpath + strSrcPlanetName + ".biom";

    if (!oArc.extract(strSrcPlanetNameWithPathAndExt, outbuff, strErr))
    {
        std::filesystem::path strarchive(wstrbiomArchiveFileName);
        strErr = "Could not extract biom file '" + strSrcPlanetName + "' from " + strarchive.string() + ". Error: " + strErr;
        return false;
    }

    // make it wide char for dest file name with correct path
    std::string strDstNamewithnested = "planetdata\\biomemaps\\" + strDstName + ".biom";
    int str_len = (int)strDstNamewithnested.length() + 1;
    int len = MultiByteToWideChar(CP_ACP, 0, strDstNamewithnested.c_str(), str_len, 0, 0);
    std::wstring wstr(len, L'\0');
    MultiByteToWideChar(CP_ACP, 0, strDstNamewithnested.c_str(), str_len, &wstr[0], len);

    wstrNewFileName =  wstrSrcFilePath + L"\\" + wstr;
    if (!_saveToFile(outbuff, wstrNewFileName, strErr))
        return false;

    return true;
} 
