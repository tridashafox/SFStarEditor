#include "espmanger.h"
#include <zlib.h>
#include <locale>
#include <codecvt>

// get and extracted the required biome content for a planet
class CArc
{
public:
    CArc() {};
    ~CArc() {};

private:
#pragma pack(push, 2) // force byte alignment
	struct ARCHdr
	{
		char m_arcTag[4];
		uint32_t m_vern;
		char m_GrnlTag[4];
		uint32_t m_nfiles;
		uint64_t m_namePos;
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
	};

	struct FNRec
	{
		FNHdr m_entry;
		FNRec(){}
		FNRec(const char *psrc) { memcpy(&m_entry, psrc, sizeof(FNHdr)); }
	};
#pragma pack(pop)

	std::vector<FNRec> m_fnrecs;
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

        if (memcmp(m_oHdr.m_GrnlTag, "GNRL", taglen) == 0)
        {
            m_fnrecs.resize(m_oHdr.m_nfiles);
            for (size_t i = 0; i < m_oHdr.m_nfiles; i++)
                m_fnrecs[i] = FNRec(&m_buffer[searchPos]);
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
            m_names.push_back(std::string(filename.data()));
        }
        return true;
    }

public:

    bool loadfile(std::vector<char> &m_buffer, const std::wstring wstrfilename, std::string& strErr)
    {
        m_buffer.clear();

        std::string strFn;
        std::filesystem::path filePath(wstrfilename);
        strFn = filePath.string();

        std::ifstream file(wstrfilename, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            strErr = std::string("Could not open archive to retrieve biome ") + strFn;
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

    bool extract(const std::wstring wstrPath, const std::string &strSrcName, const std::string &strDstName, std::vector<char>& outbuff)
    {
        // find the file rec which matches the passed name
        uint32_t fid;
        for (fid = 0; fid<m_names.size(); fid++)
            if (m_names[fid] == strSrcName)
                break;
        if (fid >= m_fnrecs.size())
            return false;

        // set up the out buff
        bool bComp = m_fnrecs[fid].m_entry.m_compsize > 0;
        std::vector<uint8_t>decompbuff;
        decompbuff.resize(m_fnrecs[fid].m_entry.m_decompsize);
        outbuff.resize(m_fnrecs[fid].m_entry.m_decompsize);

        size_t searchPtr = m_fnrecs[fid].m_entry.m_offset;
        if (bComp) 
        {
            if (searchPtr + m_fnrecs[fid].m_entry.m_compsize > m_buffer.size())
                return false;

            std::vector<uint8_t> comp(m_fnrecs[fid].m_entry.m_compsize);
            std::copy(m_buffer.begin() + searchPtr, m_buffer.begin() + searchPtr + m_fnrecs[fid].m_entry.m_compsize, comp.begin());

            // do the decompress
            uLongf destLen = static_cast<uLongf>(outbuff.size());
            uLong lsize = static_cast<uLong>(comp.size());
            if (uncompress(decompbuff.data(), &destLen, comp.data(), lsize) != Z_OK)
                return false;

            std::copy(decompbuff.begin(), decompbuff.end(), outbuff.begin());
        } 
        else 
        {
            if (searchPtr + outbuff.size() > m_buffer.size())
                return false;

            std::copy(m_buffer.begin() + searchPtr, m_buffer.begin() + searchPtr + outbuff.size(), outbuff.begin());
        }

        return true;
    }
};

bool CEsp::getBiome(const CEsp* pSrc, const std::string &strSrcName, const std::string &strDstName, std::wstring wstrNewFileName, std::string& strErr)
{
	// Find the arhive file which contains the biom needed use pSrc to help find it
    CArc oArc;
    strErr.clear();
    std::vector<char> buffer;

    std::filesystem::path filePath(pSrc->getFname());
    std::filesystem::path directory = filePath.parent_path();
    std::wstring wstrPath = directory;

    directory /= L"Starfield - PlanetData.ba2";
    std::wstring wstrfn = directory.wstring();

    if (!oArc.loadfile(buffer, wstrfn, strErr))
        return false;

    std::vector<char> outbuff;
    if (!oArc.extract(wstrPath,strSrcName, strDstName, outbuff))
         return false;

    int str_len = (int)strDstName.length() + 1;
    int len = MultiByteToWideChar(CP_ACP, 0, strDstName.c_str(), str_len, 0, 0);
    std::wstring wstr(len, L'\0');
    MultiByteToWideChar(CP_ACP, 0, strDstName.c_str(), str_len, &wstr[0], len);

    wstrNewFileName =  wstrPath + L"\\" + wstr;
    if (_saveToFile(outbuff, wstrNewFileName, strErr))
        return true;

    return false;
} 
