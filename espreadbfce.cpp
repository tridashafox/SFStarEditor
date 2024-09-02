#include "espmanger.h"


// reading of Block records used to store components

void CEsp::_readLongString(const TES4LongStringOv* &pString, const char*& searchPtr, const char*& endPtr)
{
    if (BLEFT >= sizeof(TES4LongStringOv))
    {
        pString = reinterpret_cast<const TES4LongStringOv*>(searchPtr);
        searchPtr += sizeof(pString->m_size);
        searchPtr += pString->m_size;
    }
}

void CEsp::_readCharBuff(const TES4CharBuffOv* &pCharBuff, const char*& searchPtr, const char*& endPtr)
{
    if (BLEFT >= sizeof(TES4CharBuffOv))
    {
        pCharBuff = reinterpret_cast<const TES4CharBuffOv*>(searchPtr);
        searchPtr += sizeof(pCharBuff->m_size);
        searchPtr += pCharBuff->m_size;
    }
}

// Read block records returns true if found a missing End Block marker
bool CEsp::_readBFCBtoBFBE(const char*& searchPtr, const char*& endPtr,  std::vector<CEsp::COMPRec> &oComp)
{
    size_t taglen = 4;

    // Currently treating BFCB-BFBE records as optional since a stdt/pndt can be created this way in ck
    const BFCBrecOv *pBfcb = reinterpret_cast<const BFCBrecOv*>(searchPtr);
    searchPtr += BSKIP(pBfcb); // Skip forward
    const BFCBDatarecOv *pBfcbData = reinterpret_cast<const BFCBDatarecOv*>(searchPtr);
    searchPtr += BSKIP(pBfcbData); // Skip forward

    if (oComp.size()<LIMITCOMPSTO) // FORCE A LIMIT, we don't need all the component records there can be >1.5k of these
        if (oComp.size()<=MAXCOMPINREC) // Data is bad if above this
            oComp.push_back(COMPRec(pBfcb, pBfcbData)); 

    // Skip stuff we are not intersted in the component, faster than looking for BDCE end tag
    _doBfcbquickskip(searchPtr, endPtr);

    if (BLEFT >= sizeof(BFCErecOv))
    {
        if (memcmp(searchPtr, "BFCB", taglen) == 0)
            return true; // found missing end block

        if (BLEFT >= sizeof(BFCErecOv))
        {
            if (memcmp(searchPtr, "BFCE", taglen) == 0)
            {
                // Ignore the end marker for now, it contains no useful data
                searchPtr += sizeof(BFCErecOv); // Skip forward
                return false;
            }
            else
                return true; // found something else -- bad.
        }
    }
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
        {
            if (BLEFT < taglen)
                break;

            if (memcmp(searchPtr, tag, taglen) == 0)
            {
                searchPtr += taglen;
                searchPtr += sizeof(uint16_t) + *reinterpret_cast<const uint16_t*>(searchPtr);
                bFnd = true;
            }
        }

        for (const auto& tag : otags4b)
        {
            if (BLEFT < taglen)
                break;

            if (memcmp(searchPtr, tag, taglen) == 0)
            {
                searchPtr += taglen;
                searchPtr += sizeof(uint32_t) + *reinterpret_cast<const uint32_t*>(searchPtr);
                bFnd = true;
            }
        }

        if (BLEFT < taglen)
            break;

        if (memcmp(searchPtr, "BFCE", taglen) == 0) 
            break; // found end of component block - can leave now

        if (memcmp(searchPtr, "BFCB", taglen) == 0) 
            break; // found start of a new block, shouldn't have happend

        if (bFnd)
            continue;

        searchPtr++;
    }
}