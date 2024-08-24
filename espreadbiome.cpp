#include "espmanger.h"

// get and extracted the required biome content for a planet
#pragma once
#include "zlib.h"
#include "dds.h"

class BA2
{
public:
	BA2() {};
	~BA2() {};

	bool Open(const char* fn);
	std::string fileName = "";

#pragma pack(push, 2) // force byte alignment
	struct ARCHdr
	{
		char m_arcTag[4];
		uint32_t m_vern;
		char m_GrnlTag[4];
		uint32_t m_nfiles;
		uint64_t m_namePos;
	} ARCHdr;
private:
	struct FNHdr
	{
		uint32_t nameHash;
		char ext[4];
		uint32_t dirHash;
		uint32_t flags;
		uint64_t offset;
		uint32_t packSz;
		uint32_t fullSz;
		uint32_t align;
	};

	struct FNRec
	{
		FNHdr entry;
		FNRec(){}
		FNRec(FILE* fo) { fread(&entry, sizeof(FNHdr), 1, fo); }
	};
#pragma pack(pop)

	std::vector<FNRec> m_fnrecs;
};

bool BA2::Open(const char* fn)
{
	FILE* file = fopen(fn, "rb");
	if (!file)
		return false;

	fread(&ARCHdr, sizeof(ARCHdr), 1, file);

	if (memcmp(ARCHdr.m_arcTag, "BTDX", sizeof(ARCHdr.m_arcTag)) != 0)
		return false;

	fileName = std::string(fn);
	if (memcmp(ARCHdr.m_GrnlTag, "GNRL", sizeof(ARCHdr.m_GrnlTag)) == 0)
	{
		m_fnrecs.resize(ARCHdr.m_nfiles);
		for (int i = 0; i < ARCHdr.m_nfiles; i++)
			m_fnrecs[i]=FNRec(file);
	}
	else
		return false;

	_fseeki64(file, ARCHdr.m_namePos, SEEK_SET);
	for (int i = 0; i < ARCHdr.m_nfiles; i++)
	{
		uint16_t len;
		fread(&len, 2, 1, file);
		std::vector<char> fn(len + 1);
		fread(&fn[0], 1, len, file);
		fn[len] = '\0';
		m_names.push_back(std::string(&fn[0]));
	}
	fclose(file);
	return true;
}



// Function to extract a file from a BA2 archive by its name
bool extractFileFromBA2(const std::string& archiveName, const std::string& fileName, const std::string& outputPath) 
{
    // Load the BA2 archive
    BA2Archive archive;
    if (!archive.load(archiveName)) {
        std::cerr << "Failed to load archive: " << archiveName << std::endl;
        return false;
    }

    // Locate the file in the archive
    const BA2FNHdr* entry = archive.findFile(fileName);
    if (!entry) {
        std::cerr << "File not found in archive: " << fileName << std::endl;
        return false;
    }

    // Open the file for output
    std::ofstream outFile(outputPath, std::ios::binary);
    if (!outFile.is_open()) {
        std::cerr << "Failed to open output file: " << outputPath << std::endl;
        return false;
    }

    // Read and decompress the file from the archive
    std::vector<char> fileData;
    if (!archive.extractFileData(*entry, fileData)) {
        std::cerr << "Failed to extract file data: " << fileName << std::endl;
        return false;
    }

    // Write the extracted data to the output file
    outFile.write(fileData.data(), fileData.size());
    outFile.close();

    return true;
}


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
		uint32_t nameHash;
		char ext[4];
		uint32_t dirHash;
		uint32_t flags;
		uint64_t offset;
		uint32_t packSz;
		uint32_t fullSz;
		uint32_t align;
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
    std::vector<char>& m_buffer;
    ARCHdr m_oHdr;

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
        return true;
    }

    bool loadHdrs()
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
            for (int i = 0; i < m_oHdr.m_nfiles; i++)
                m_fnrecs[i] = FNRec(&m_buffer[searchPos]);
        }
        else
            return false;

        // Get names
        if (m_oHdr.m_namePos >= m_buffer.size())
            return false;

        searchPos = m_oHdr.m_namePos;

        // Read file names
        for (int i = 0; i < m_oHdr.m_nfiles; i++)
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

    bool extract(const std::wstring wstrPath, const std::string &strSrcName, const std::string &strDstName, std::string &strErr)
    {
        // TODO:
        // find the file rec which matches the passed name

        return true;
    }

    bool extract(uint32_t fid, std::vector<uint8_t>& outbuff) 
    {
        if (fid >= m_fnrecs.size())
            return false;

        bool bComp = m_fnrecs[fid].m_entry.packSz > 0;
        outbuff.resize(m_fnrecs[fid].m_entry.fullSz);

        size_t searchPtr = m_fnrecs[fid].m_entry.offset;
        if (bComp) 
        {
            if (searchPtr + m_fnrecs[fid].m_entry.packSz > m_buffer.size())
                return false;

            std::vector<uint8_t> comp(m_fnrecs[fid].m_entry.packSz);
            std::copy(m_buffer.begin() + searchPtr, m_buffer.begin() + searchPtr + m_fnrecs[fid].m_entry.packSz, comp.begin());

            uLongf destLen = static_cast<uLongf>(outbuff.size());
            if (uncompress(outbuff.data(), &destLen, comp.data(), comp.size()) != Z_OK)
                return false;
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


bool CEsp::getBiome(const CEsp* pSrc, const std::string &strSrcName, const std::string &strDstName, std::string& strErr)
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

	// load the ARCHdr and m_fnEntry records
    if (!oArc.extract(wstrPath,strSrcName, strDstName, strErr))
         return false;

    return true;

}
