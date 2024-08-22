#include "espmanger.h"


CEsp::formid_t CEsp::_createNewFormId()
{
    HEDRHdrOv* pMutableHedr = const_cast<HEDRHdrOv*>(getHEDRHdr_ptr()); 

    // must not be zero, there should at least one or more objects already in the ESP, likely means the ptr is bad
    if (!pMutableHedr->m_nextobjectid)
        return 0; // Dont modify the value of it is zero

    pMutableHedr->m_nextobjectid++; // update hdr value for next form id
    formid_t newformid = pMutableHedr->m_nextobjectid;
    newformid &= ESP_FORMIDMASK; // mask off any value in top byte
    newformid |= ESP_FORMIDPREF; // set the prefix byte - seems to be 0x01 for ESPs

    return newformid; // return and Update the form id in the header
}

CEsp::formid_t CEsp::_createNewSystemId()
{
    // TODO - don't know, it looks like a form id in the esm with current max being 0x0001D1B9
    // creating a new star in CK results in planet system id of 11AD7 for the first one created all others are the same
    // It kind of look like these where created outside of the CK.
    // Using form ID for now

    return _createNewFormId();
}

uint32_t CEsp::_incNumObjects()
{
    HEDRHdrOv * pMutableHdr = const_cast<HEDRHdrOv*>(getHEDRHdr_ptr()); 
    return pMutableHdr->m_recordcount++;
}

CEsp::GenBlock* CEsp::_makegenblock(const char *tag, const void* pdata, size_t ilen) 
{
    size_t totalSize = sizeof(GenBlock) + ilen; // Size of struct + string length, space for null already in struct

    // check the length makes sense
    if (ilen > 0xFFFF)
        return nullptr;

    uint16_t ilen16 = static_cast<uint16_t>(ilen);

    // Allocate memory for the struct plus the string
    GenBlock* pnewblk = (GenBlock*)malloc(totalSize);
    if (pnewblk == nullptr)
        return nullptr;

    memcpy(pnewblk->m_tag, tag, sizeof(pnewblk->m_tag));
    pnewblk->m_size = ilen16;
    char *pdest = reinterpret_cast<char *>(&pnewblk->m_aname);
    memcpy(pdest, pdata, ilen16); 

    return pnewblk;
}
CEsp::GenBlock* CEsp::_makegenblock(const char* tag, const char* pdata)
{
    return _makegenblock(tag, (void*)pdata, strlen(pdata)+1); // +1 for null terminater
}

void CEsp::_addtobuff(std::vector<char>& buffer, void* pdata, size_t datasize) 
{
    size_t currentSize = buffer.size();
    buffer.resize(currentSize + datasize);
    std::memcpy(buffer.data() + currentSize, pdata, datasize);
}

// insert a string into the block of memory over existing string and update the size
void CEsp::_insertbuff(std::vector<char>& newbuff, char* pDstName, uint16_t* pSizeToFixup, const char* pNewbuff, size_t iSizeNewBuffer)
{
    if (!newbuff.size() || !pDstName || !pSizeToFixup || !pNewbuff || !iSizeNewBuffer) 
        return;

    {   // points inside {} will be invalid after insert/erase operations below
        size_t oldSize = *pSizeToFixup;
        size_t newSize = iSizeNewBuffer; // +1 for null terminator
        size_t iSaveSizeoffset = (char*)pSizeToFixup - newbuff.data();

        // Calculate the position of the destination name within the buffer
        ptrdiff_t offset = pDstName - newbuff.data();

        // Resize the buffer to accommodate the new name
        if (newSize != oldSize)
        {
            // If the new name is larger, expand the buffer
            if (newSize > oldSize)
                newbuff.insert(newbuff.begin() + offset + oldSize, newSize - oldSize, 0);
            else
                newbuff.erase(newbuff.begin() + offset + newSize, newbuff.begin() + offset + oldSize);
        }

        // Copy the new name into the buffer at the correct location
        memcpy(newbuff.data() + offset, pNewbuff, newSize);

        // Update size
        *((uint16_t*)(newbuff.data() + iSaveSizeoffset)) =  static_cast<uint16_t>(newSize); // do like this in case the orginal size pointer value changed
    }

    // Rebuild oRec because above will have moved memory around invalidating pointers
} 


// insert a string into the block of memory over existing string and update the size
void CEsp::_insertname(std::vector<char>& newbuff, char* pDstName, uint16_t* pNameSize, const char* pNewName)
{
    if (!newbuff.size() || !pNameSize || !*pNameSize || !pDstName || !pNewName || !*pDstName || !*pNewName || strlen(pNewName)>0xFF) 
        return;

    {   // points inside {} will be invalid after insert/erase operations below
        size_t oldNameSize = *pNameSize;
        size_t newNameSize = strlen(pNewName)+1; // +1 for null terminator
        size_t iSaveSizeoffset = (char*)pNameSize - newbuff.data();

        // Calculate the position of the destination name within the buffer
        ptrdiff_t offset = pDstName - newbuff.data();
        std::string strSafeNewName(pNewName); // Make a copy before mem moves below as it could break any pointers

        // Resize the buffer to accommodate the new name
        if (newNameSize != oldNameSize)
        {
            // If the new name is larger, expand the buffer
            if (newNameSize > oldNameSize)
                newbuff.insert(newbuff.begin() + offset + oldNameSize, newNameSize - oldNameSize, 0);
            else
                newbuff.erase(newbuff.begin() + offset + newNameSize, newbuff.begin() + offset + oldNameSize);
        }

        // Copy the new name into the buffer at the correct location
        memcpy(newbuff.data() + offset, strSafeNewName.c_str(), newNameSize);

        // Update size
        *((uint16_t*)(newbuff.data() + iSaveSizeoffset)) =  static_cast<uint16_t>(newNameSize); // do like this in case the orginal size pointer value changed
    }

    // Rebuild oRec because above will have moved memory around invalidating pointers
} 


// For the passed group append the passed new record 
bool CEsp::appendToGrup(char *pgrup, const std::vector<char>& insertData)
{
    if (!pgrup || !insertData.size())
        return false;

    GRUPHdrOv* pGrupHdr = reinterpret_cast<GRUPHdrOv*>(pgrup);
    if (!pGrupHdr->m_size)
        return false;

    size_t grupOffset = pgrup - m_buffer.data();
    size_t newGrupSize = pGrupHdr->m_size + insertData.size();
    size_t insertoffset = grupOffset + pGrupHdr->m_size;

    size_t oldBufferSize = m_buffer.size();
    m_buffer.resize(oldBufferSize + insertData.size());

    // Move existing data after the GRUP section to make room for newbuff.
    memmove(m_buffer.data() + insertoffset + insertData.size(), m_buffer.data() + insertoffset, oldBufferSize - insertoffset);

    // Insert the new data into the buffer at the correct position.
    std::copy(insertData.begin(), insertData.end(), m_buffer.begin() + insertoffset);

    // Get the new address of pgrup after resizing m_buffer as ptr might have changed and update grup size
    pgrup = m_buffer.data() + grupOffset;
    pGrupHdr = reinterpret_cast<GRUPHdrOv*>(pgrup);
    pGrupHdr->m_size = static_cast<uint32_t>(newGrupSize);

    _incNumObjects(); // +1 for new object in insertData

    // reload this esp with data inserted as pointer references will have been broken
    std::string strErrReload;
    if (!_loadfrombuffer(strErrReload))
        return false;

    return true;
}

// Create a new group and add in the passed record in newbuff for cases where no grup already exists
bool CEsp::createGrup(const char *pTag, const std::vector<char> &newBuff)
{
    if (!newBuff.size() || strlen(pTag) != 4)
        return false;

    // allocate new Grup
    uint32_t grpsize = sizeof(GRUPHdrOv) + sizeof(GRUPBlankOv);
    std::vector<char> newGrup(grpsize);

    // set the values
    GRUPHdrOv *pGrpHdr = (GRUPHdrOv*)&newGrup[0];
    GRUPBlankOv *pGrpBlk = (GRUPBlankOv*)&newGrup[sizeof(GRUPHdrOv)];
    memcpy(pGrpHdr->m_GRUPtag, "GRUP", sizeof(pGrpHdr->m_GRUPtag));
    memcpy(pGrpBlk->m_tag, pTag, sizeof(pGrpBlk->m_tag));
    pGrpBlk->m_flags = 0x3106; // TODO: what do these mean exactly, hardwired for now

    // Set total size before doing insert since this could result in different address space
    pGrpHdr->m_size = grpsize +  static_cast<uint32_t>(newBuff.size());

    // Append the new group to the dst buffer
    m_buffer.insert(m_buffer.end(), newGrup.begin(), newGrup.end());
    m_buffer.insert(m_buffer.end(), newBuff.begin(), newBuff.end());

    _incNumObjects(); // +1 For new grup
    _incNumObjects(); // +1 for new object in newBuff

    // reload this esp with data inserted as pointer references will have been broken
    std::string strErrReload;
    if (!_loadfrombuffer(strErrReload))
        return false;

    return true;
}

// Sets the record size to the size of the memory buffer, minus the header size
bool CEsp::_refreshsizeStdt(std::vector<char>& newbuff, const STDTrec& oRec)
{
    STDTHdrOv* pMutableHdr = const_cast<STDTHdrOv*>(oRec.m_pHdr);  // recalc ptr due to mem changes
    if (newbuff.size() < sizeof(STDTHdrOv))
        return false; // buffer too small for valid record

    pMutableHdr->m_size = static_cast<uint32_t>((newbuff.size() - sizeof(STDTHdrOv))); // Size of STDT record excludes the header, why no one knows
    return true;
}
       
void CEsp::_rebuildStdtRecFromBuffer(STDTrec &oRec, const std::vector<char>&newstdbuff)
{
    oRec.m_pHdr = reinterpret_cast<const STDTHdrOv*>(&newstdbuff[0]);
    const char* searchPtr = (char*)oRec.m_pHdr + sizeof(STDTHdrOv); // Skip past the header
    const char* endPtr = &newstdbuff[newstdbuff.size() - 1];
    _dostdt_op_findparts(oRec, searchPtr, endPtr);
}

// need to fix up component records which where cloned and came from an ESM
void CEsp::_clonefixupcompsStdt(std::vector<char>& newbuff, STDTrec& oRec)
{
    for (const auto& pComp : oRec.m_oComp)
    {
        char* pcompname = (char*)&pComp.m_pBfcbName->m_name;
        std::string strcname(pcompname);
        if (strcname == "TESFullName_Component")
        {
            if (pComp.m_pBfcbName->m_size > sizeof(FULLrecOv)) // FULL
            {
                // Suff in the acutal name here. We might replacing cloned data or some offset value
                FULLrecOv* pFULLrecOv = reinterpret_cast<FULLrecOv*>(&pcompname[strcname.size()+1]);

                // insert will rebuild oRec after its changes at this point in the fixes oRec has the new name so we dont need BasicInfo
                _insertname(newbuff, (char *)&pFULLrecOv->m_name, (uint16_t *)&pFULLrecOv->m_size, (char *)&oRec.m_pAnam->m_aname);
                _rebuildStdtRecFromBuffer(oRec, newbuff); // reload info due to memory changes
                _refreshsizeStdt(newbuff, oRec); // refresh sizes due to memory changes - just sets the in the Hdr to the size of the buffer
                break;
            }
        }
        else
        if (strcname == "HoudiniData_Component") // XXXX(4byte size) - PCCC then BETH then etc
        {
            // TODO - doesnt not look like it is required - possibly the first
            // planet_name^[ 0^locks=0 ]^(^"Alpha Centauri"^)& --- ^=0x09, &=0x0A
            // All output filenames will include this name*name\0 --- *= skip 8 bytes string then null terminator
        }
    }
}

// Creates the required star location record from the info and returns it in the newLocBuff
// the input buffer newStarbuff should old the cloned and modified star so we can get some info from it
// The basic info will the level information and other info not held with the star but must be put in the location
bool CEsp::createLocStar(const std::vector<char> &newStarbuff, const BasicInfoRec &oBasicInfo, std::vector<char> &newLocbuff)
{
    uint16_t taglen = 4;
    size_t totalsize = 0;

    // Create an oRec from the passed star buffer so we can easily get info from it
    STDTrec oRec = {};
    _rebuildStdtRecFromBuffer(oRec, newStarbuff);

    // Some macros to prevent typos
    #define _setcomm(x, y) memcpy(&x, y, taglen); x.m_size = sizeof(x) - (taglen + sizeof(x.m_size))
    #define _setcommhdr(x, y) memcpy(&x, y, taglen); x.m_size = 0
    #define _addrecsize(x) (x.m_size + taglen + sizeof(x.m_size))
    #define _paddrecsize(x) (x->m_size + taglen + sizeof(x->m_size))

    // { } used to prevent typoes from cut/paste by enforcing scope
    {// LCTN header
        LCTNHdrOv ohdr;
        _setcommhdr(ohdr, "LCTN"); // hdr size excludes size of hdr record
        ohdr.m_formid = _createNewFormId();
        totalsize = ohdr.m_size;
        _addtobuff(newLocbuff, &ohdr, sizeof(ohdr));
    }

    const char* pAname = reinterpret_cast<const char*>(&oRec.m_pAnam->m_aname);
    {// EDID Editor id - name
        uint32_t size = 0;
        EDIDrecOv* pEdid = reinterpret_cast<EDIDrecOv*>(_makegenblock("EDID", std::string("S" + std::string(pAname)).c_str()));
        totalsize += size = _paddrecsize(pEdid);
        _addtobuff(newLocbuff, pEdid, size);
    }

    {// FULL full name for location
        uint32_t size = 0;
        FULLrecOv* pFull = reinterpret_cast<FULLrecOv*>(_makegenblock("FULL", pAname));
        totalsize += size = _paddrecsize(pFull);
        _addtobuff(newLocbuff, pFull, size);
    }

    {// KSIZ Keywords how many records   
        KSIZrecOv oKsiz;
        _setcomm(oKsiz, "KSIZ");
        oKsiz.m_count = 1;
        totalsize += sizeof(oKsiz);
        _addtobuff(newLocbuff, &oKsiz, sizeof(oKsiz));
    }

    {// KWDA Keyword(s) data - one keyword LocTypeStarSystem
        uint32_t size = 0;
        uint32_t keywords[] = { KW_LocTypeStarSystem }; 
        KWDArecOv* pKwda = reinterpret_cast<KWDArecOv*>(_makegenblock("KWDA", &keywords, sizeof(keywords)));
        totalsize += size = _paddrecsize(pKwda);
        _addtobuff(newLocbuff, pKwda, size);
    }
        
    { // DATA data record of location
        LCTNDataOv oData;
        _setcomm(oData, "DATA");
        oData.m_faction = static_cast<CEsp::formid_t>(oBasicInfo.m_iFaction);
        oData.m_playerLvl = static_cast<uint8_t>(oBasicInfo.m_iSysPlayerLvl);
        oData.m_playerLvlMax = static_cast<uint8_t>(oBasicInfo.m_iSysPlayerLvlMax);
        totalsize += _addrecsize(oData);
        _addtobuff(newLocbuff, &oData, sizeof(oData));
    }

    {// PNAM Parent of the star - Universe
        LCTNPnamOv oPnam;
        _setcomm(oPnam, "PNAM");
        oPnam.m_parentloc = FID_Universe;
        totalsize += _addrecsize(oPnam);
        _addtobuff(newLocbuff, &oPnam, sizeof(oPnam));
    }

    { // XNAM Star system id
        LCTNXnamOv oXnam;
        _setcomm(oXnam, "XNAM");
        oXnam.m_systemId = oRec.m_pDnam->m_systemId;
        totalsize += _addrecsize(oXnam);
        _addtobuff(newLocbuff, &oXnam, sizeof(oXnam));
    }

    // put the new size into the buffer hdr
    LCTNHdrOv *pHdr = reinterpret_cast<LCTNHdrOv*>(newLocbuff.data());
    pHdr->m_size = static_cast<uint32_t>(totalsize);

    return true;
}


// Make a clone of the passed record in ostdRec, chnaging the clone with the info oBasicInfo
// put the result in newbuff
bool CEsp::cloneStdt(std::vector<char> &newbuff, const STDTrec &ostdtRec, const BasicInfoRec &oBasicInfo)
{
    if (!m_buffer.size() || !*oBasicInfo.m_pName || ostdtRec.m_isBad || !ostdtRec.m_pEdid->m_name)
        return false;

    // make newbuff the size of the STDT specified in the hdr and copy it over from the source
    // Note unlike GRUP records, the size of the record does not include the size of the STDT header 
    size_t sizeofstdntoclone = ostdtRec.m_pHdr->m_size + sizeof(STDTHdrOv); 
    newbuff.resize(sizeofstdntoclone);
    memcpy(&newbuff[0], &ostdtRec.m_pHdr[0], sizeofstdntoclone);

    // Build an STDTrec so it will be easier to patch up the names etc
    STDTrec oRec = {};
    _rebuildStdtRecFromBuffer(oRec, newbuff);
    if (!oRec.m_pHdr)
        return false; // should not happen bad hdr ptr

    // For time being override const, alot of reworking required to make this clear which could get through away if more OV added
    STDTHdrOv * pMutableHdr = const_cast<STDTHdrOv*>(oRec.m_pHdr); 
    if (!pMutableHdr)
        return false;

    pMutableHdr->m_formid = _createNewFormId();
    if (!pMutableHdr->m_formid)
        return false; // id should not be zero, possible bad record

    if (!oRec.m_pBnam || !oRec.m_pDnam)
        return false; // Bad position pointer, or bad System id pointer

    STDTBnamOv* pMutableBnam = const_cast<STDTBnamOv*>(oRec.m_pBnam); 
    if (!pMutableBnam)
        return false;
    pMutableBnam->m_xPos = oBasicInfo.m_StarMapPostion.m_xPos;
    pMutableBnam->m_yPos = oBasicInfo.m_StarMapPostion.m_yPos;
    pMutableBnam->m_zPos = oBasicInfo.m_StarMapPostion.m_zPos;

    STDTDnamOv * pMutableDnam = const_cast<STDTDnamOv*>(oRec.m_pDnam); 
    if (!pMutableDnam)
        return false;
    pMutableDnam->m_systemId = _createNewSystemId();

    if (!oRec.m_pEdid || !oRec.m_pEdid->m_size || !oRec.m_pEdid->m_name)
        return false; // bad editor name

    // change the names. oRec is passed because it will be rebuilt after each of these operations since
    // memory addresses will change as things are moved around and re-allocations happen
    _insertname(newbuff, (char *)&oRec.m_pEdid->m_name, (uint16_t *)&oRec.m_pEdid->m_size, oBasicInfo.m_pName);
    _rebuildStdtRecFromBuffer(oRec, newbuff); // reload info due to memory changes
    _refreshsizeStdt(newbuff, oRec); // refresh sizes due to memory changes - just sets the in the Hdr to the size of the buffer

    if (!oRec.m_pAnam || !oRec.m_pAnam->m_size || !oRec.m_pAnam->m_aname)
        return false; // Bad full name

    _insertname(newbuff, (char *)&oRec.m_pAnam->m_aname, (uint16_t *)&oRec.m_pAnam->m_size, oBasicInfo.m_pAName);
    _rebuildStdtRecFromBuffer(oRec, newbuff); // reload info due to memory changes
    _refreshsizeStdt(newbuff, oRec); // refresh sizes due to memory changes - just sets the in the Hdr to the size of the buffer

    // Ouch, Now fix up some records which don't work in an ESP if taken from an ESM
    _clonefixupcompsStdt(newbuff, oRec);

    return true;
}

// Makes a star by cloning one from passed source ESM and updating it with info passed in oBasicInfo
//
// 1. Makes a clone of a STDT block of data from the source Esp file buffer
// 2. Maps the record structures onto the clone so values can over written to be the new ones (e.g. new name)
// 3. Insert the clone into the Dst Eps buffer at the end of the STDT group records
// 4. Makes location records as required for STDT 
bool CEsp::makestar(const CEsp *pSrc, const BasicInfoRec &oBasicInfo, std::string &strErr)
{
    std::vector<char> newStarbuff;
    std::vector<char> newLocbuff;

    if (!pSrc)
        MKFAIL("No source provided to create the star from.");

    // Check values make sense, since we will be casting these down to uint8
    if (oBasicInfo.m_iSysPlayerLvl > 0xFF || oBasicInfo.m_iSysPlayerLvlMax > 0xFF || oBasicInfo.m_iPlanetPos >0xFF)
        MKFAIL("Could not create star as the values Player Level, or Planet Positon are larger than 255.");

    // Create and save into m_buffer the new star
    if (!cloneStdt(newStarbuff, pSrc->m_stdts[oBasicInfo.m_iIdx], oBasicInfo))
        MKFAIL("Could not clone selected star.");

    m_bIsSaved = false;

    if (!m_grupStdtPtrs.size()) // no group for STDTs exists in the destination esp
    {
        // Create a new group from scratch and add to end of file
        if (!createGrup("STDT", newStarbuff))
            MKFAIL("Could not add new group record with star.")
    }
    else
    {
        if (!appendToGrup((char*)m_grupStdtPtrs[0], newStarbuff))
            MKFAIL("Could not add new star to existing destination group star record.");
    }

    // Create locations from modified star buffer 
    if (!createLocStar(newStarbuff, oBasicInfo, newLocbuff))
        MKFAIL("Could not create location record for new star.");

    // Save the locaiton to the location group if it exists otherwise create a new group and add it to that
    if (!m_grupLctnPtrs.size())
    {
        // Create a new group from scratch and add to end of file
        if (!createGrup("LCTN", newLocbuff))
            MKFAIL("Could not add new group record with location.");
    }
    else
    {
        if (!appendToGrup((char*)m_grupLctnPtrs[0], newLocbuff))
            MKFAIL("Could not add new location to existing destination group location record.");
    }

    return true;
}

// Creates the required planet location record from the info and returns it in the newLocBuff
// the input buffer newPlanetbuff should hold the cloned and modified planet so we can get some info from it
// The basic info will the level information and other info not held with the star but must be put in the location
// Need Planet -> surface, orbit
bool CEsp::createLocPlanet(const std::vector<char> &newPlanetbuff, const BasicInfoRec &oBasicInfo, std::vector<char> &newLocbuff)
{
    return true;

    uint16_t taglen = 4;
    size_t totalsize = 0;

    // Create an oRec from the passed star buffer so we can easily get info from it
    PNDTrec oRec = {};
    _rebuildPndtRecFromBuffer(oRec, newPlanetbuff, true);

    // TODO:

    /*
    // Some macros to prevent typos
    #define _setcomm(x, y) memcpy(&x, y, taglen); x.m_size = sizeof(x) - (taglen + sizeof(x.m_size))
    #define _setcommhdr(x, y) memcpy(&x, y, taglen); x.m_size = 0
    #define _addrecsize(x) (x.m_size + taglen + sizeof(x.m_size))
    #define _paddrecsize(x) (x->m_size + taglen + sizeof(x->m_size))

    // { } used to prevent typoes from cut/paste by enforcing scope
    {// LCTN header
        LCTNHdrOv ohdr;
        _setcommhdr(ohdr, "LCTN"); // hdr size excludes size of hdr record
        ohdr.m_formid = _createNewFormId();
        totalsize = ohdr.m_size;
        _addtobuff(newLocbuff, &ohdr, sizeof(ohdr));
    }

    const char* pAname = reinterpret_cast<const char*>(&oRec.m_pAnam->m_aname);
    {// EDID Editor id - name
        uint32_t size = 0;
        EDIDrecOv* pEdid = reinterpret_cast<EDIDrecOv*>(_makegenblock("EDID", std::string("S" + std::string(pAname)).c_str()));
        totalsize += size = _paddrecsize(pEdid);
        _addtobuff(newLocbuff, pEdid, size);
    }

    {// FULL full name for location
        uint32_t size = 0;
        FULLrecOv* pFull = reinterpret_cast<FULLrecOv*>(_makegenblock("FULL", pAname));
        totalsize += size = _paddrecsize(pFull);
        _addtobuff(newLocbuff, pFull, size);
    }

    {// KSIZ Keywords how many records   
        KSIZrecOv oKsiz;
        _setcomm(oKsiz, "KSIZ");
        oKsiz.m_count = 1;
        totalsize += sizeof(oKsiz);
        _addtobuff(newLocbuff, &oKsiz, sizeof(oKsiz));
    }

    {// KWDA Keyword(s) data - one keyword LocTypeStarSystem
        uint32_t size = 0;
        uint32_t keywords[] = { KW_LocTypeStarSystem }; 
        KWDArecOv* pKwda = reinterpret_cast<KWDArecOv*>(_makegenblock("KWDA", &keywords, sizeof(keywords)));
        totalsize += size = _paddrecsize(pKwda);
        _addtobuff(newLocbuff, pKwda, size);
    }
        
    { // DATA data record of location
        LCTNDataOv oData;
        _setcomm(oData, "DATA");
        oData.m_faction = static_cast<CEsp::formid_t>(oBasicInfo.m_iFaction);
        oData.m_playerLvl = static_cast<uint8_t>(oBasicInfo.m_iSysPlayerLvl);
        oData.m_playerLvlMax = static_cast<uint8_t>(oBasicInfo.m_iSysPlayerLvlMax);
        totalsize += _addrecsize(oData);
        _addtobuff(newLocbuff, &oData, sizeof(oData));
    }

    {// PNAM Parent of the star - Universe
        LCTNPnamOv oPnam;
        _setcomm(oPnam, "PNAM");
        oPnam.m_parentloc = FID_Universe;
        totalsize += _addrecsize(oPnam);
        _addtobuff(newLocbuff, &oPnam, sizeof(oPnam));
    }

    { // XNAM Star system id
        LCTNXnamOv oXnam;
        _setcomm(oXnam, "XNAM");
        oXnam.m_systemId = oRec.m_pDnam->m_systemId;
        totalsize += _addrecsize(oXnam);
        _addtobuff(newLocbuff, &oXnam, sizeof(oXnam));
    }
    */

    // put the new size into the buffer hdr
    LCTNHdrOv *pHdr = reinterpret_cast<LCTNHdrOv*>(newLocbuff.data());
    pHdr->m_size = static_cast<uint32_t>(totalsize); 

    return true;
}

// Sets the record size to the size of the memory buffer, minus the header size
bool CEsp::_refreshsizePndt(std::vector<char>& newbuff, const PNDTrec& oRec)
{
    if (!oRec.m_pHdr)
        return false;

    PNDTHdrOv* pMutableHdr = const_cast<PNDTHdrOv*>(oRec.m_pHdr);  // recalc ptr due to mem changes
    if (!pMutableHdr)
        return false;

    if (newbuff.size() < sizeof(PNDTHdrOv))
        return false; // buffer too small for valid record

    pMutableHdr->m_size = static_cast<uint32_t>((newbuff.size() - sizeof(PNDTHdrOv))); // Size of STDT record excludes the header, why no one knows
    pMutableHdr->m_decompsize = static_cast<uint32_t>(newbuff.size());

    return true;
}

void CEsp::_rebuildPndtRecFromBuffer(PNDTrec &oRec, const std::vector<char>&newpndbuff, bool bDecomp)
{
    // decompress the data in the copy of the source planet data 
    oRec.m_pHdr = reinterpret_cast<const PNDTHdrOv*>(&newpndbuff[0]);

    if (bDecomp)
    {
        oRec.m_pcompdata = &newpndbuff[sizeof(PNDTHdrOv)];
        oRec.m_compdatasize = oRec.m_pHdr->m_size - sizeof(oRec.m_pHdr->m_decompsize);  
        decompress_data(oRec.m_pcompdata, oRec.m_compdatasize, oRec.m_decompdata, oRec.m_pHdr->m_decompsize);
    }

    // build out other PNDT data records
    const char* searchPtr = &oRec.m_decompdata[0];
    const char* endPtr = &oRec.m_decompdata[oRec.m_decompdata.size() - 1];

    _dopndt_op_findparts(oRec, searchPtr, endPtr);
}

size_t CEsp::_adjustPlanetPositions(const STDTrec &ostdtRec, size_t iPlanetPos)
{
    // change all the other planets in the system's id to allow iPlanetPos to fit it
    // Insert at iPlantPos, incrementation all in > iPlantPos
    // Insert at iPlantPos > last pos, add to the end
    // No nother planets nothing to do
    // 
    // TODO:

    return iPlanetPos;
}

// Make a clone of the passed record in ostdRec, chnaging the clone with the info oBasicInfo
// put the result in newbuff
bool CEsp::clonePndt(std::vector<char> &newbuff, const PNDTrec &opndtRec, const BasicInfoRec &oBasicInfo)
{
    if (!m_buffer.size() || !*oBasicInfo.m_pName || opndtRec.m_isBad || !opndtRec.m_pEdid->m_name)
        return false;

    // make newbuff the size of the STDT specified in the hdr and copy it over from the source
    // Note unlike GRUP records, the size of the record does not include the size of the STDT header 

    size_t sizeofpndntoclone = opndtRec.m_pHdr->m_size + sizeof(PNDTHdrOv); 
    newbuff.resize(sizeofpndntoclone);
    memcpy(&newbuff[0], &opndtRec.m_pHdr[0], sizeofpndntoclone);

    // Build an PNDTrec so it will be easier to patch up the names etc
    PNDTrec oRec = {};
    _rebuildPndtRecFromBuffer(oRec, newbuff, true);
    if (!oRec.m_pHdr)
        return false; // should not happen bad hdr ptr

    // For time being override const, alot of reworking required to make this clear which could get through away if more OV added
    PNDTHdrOv * pMutableHdr = const_cast<PNDTHdrOv*>(oRec.m_pHdr); 
    if (!pMutableHdr)
        return false;

    pMutableHdr->m_formid = _createNewFormId();
    if (!pMutableHdr->m_formid)
        return false; // id should not be zero, possible bad record

        // Do some validation because a change is better than a rest 
    if (oBasicInfo.m_iPrimaryIdx > m_stdts.size() || !m_stdts[oBasicInfo.m_iPrimaryIdx].m_pDnam->m_systemId || 
        !m_stdts[oBasicInfo.m_iPrimaryIdx].m_pDnam)
        return false; // bad primary index

    // Put the planet in the star system of the star indx in PirmaryIdx
    formid_t iSystem = m_stdts[oBasicInfo.m_iPrimaryIdx].m_pDnam->m_systemId;
    PNDTGnamOv * pMutableGnam = const_cast<PNDTGnamOv*>(oRec.m_pGnam); 
    if (!pMutableGnam)
        return false;

    pMutableGnam->m_systemId = iSystem;
    pMutableGnam->m_primePndtId = static_cast<uint32_t>(oBasicInfo.m_iPlanetPos); 
    
    if (!oRec.m_pEdid || !oRec.m_pEdid->m_size || !oRec.m_pEdid->m_name)
        return false; // bad editor name

    // Do work below in the decompressed buffer
    _insertname(oRec.m_decompdata, (char *)&oRec.m_pEdid->m_name, (uint16_t *)&oRec.m_pEdid->m_size, oBasicInfo.m_pName);
    _rebuildPndtRecFromBuffer(oRec, oRec.m_decompdata, false); // reload info due to memory changes
    _refreshsizePndt(oRec.m_decompdata, oRec); // refresh sizes due to memory changes - just sets the in the Hdr to the size of the buffer
   

    if (!oRec.m_pAnam || !oRec.m_pAnam->m_size || !oRec.m_pAnam->m_aname)
        return false; // Bad full name

    _insertname(oRec.m_decompdata, (char *)&oRec.m_pAnam->m_aname, (uint16_t *)&oRec.m_pAnam->m_size, oBasicInfo.m_pAName);
    _rebuildPndtRecFromBuffer(oRec, oRec.m_decompdata, false); // reload info due to memory changes
    _refreshsizePndt(oRec.m_decompdata, oRec); // refresh sizes due to memory changes - just sets the in the Hdr to the size of the buffer

    // Recompress the compressed buffer out side the buffer, then insert it overtop the current compressed buffer

    std::vector<char> newcompressbuffer;
    if (!compress_data(oRec.m_decompdata.data(), oRec.m_pHdr->m_decompsize, newcompressbuffer))
        return false;

    _insertbuff(newbuff, (char *)oRec.m_pcompdata, (uint16_t *)&oRec.m_compdatasize, newcompressbuffer.data(), newcompressbuffer.size());
    _rebuildPndtRecFromBuffer(oRec, oRec.m_decompdata, true); // reload info due to memory changes
    _refreshsizePndt(oRec.m_decompdata, oRec); // refresh sizes due to memory changes - just sets the in the Hdr to the size of the buffer

    // Ouch, Now fix up some records which don't work in an ESP if taken from an ESM
    //_clonefixupcompsStdt(newbuff, oRec);

    return true;
}

bool CEsp::makeplanet(const CEsp* pSrc, const BasicInfoRec& oBasicInfo, std::string& strErr)
{
    strErr.clear();
    // todo: create clone with changes and locations

    std::vector<char> newPlanetbuff;
    std::vector<char> newLocbuff;

    if (!pSrc)
        MKFAIL("No source provided to create the planet from.");

    // Check values make sense, since we will be casting these down to uint8
    if (oBasicInfo.m_iSysPlayerLvl > 0xFF || oBasicInfo.m_iSysPlayerLvlMax > 0xFF || oBasicInfo.m_iPlanetPos >0xFF)
        MKFAIL("Could not create planet as the values Player Level, or Planet Positon are larger than 255.");

    // Create and save into m_buffer the new star
    if (!clonePndt(newPlanetbuff, pSrc->m_pndts[oBasicInfo.m_iIdx], oBasicInfo))
        MKFAIL("Could not clone selected planet.");

    m_bIsSaved = false;

    if (!m_grupPndtPtrs.size()) // no group for STDTs exists in the destination esp
    {
        // Create a new group from scratch and add to end of file
        if (!createGrup("PNDT", newPlanetbuff))
            MKFAIL("Could not add new group record with planet.")
    }
    else
    {
        if (!appendToGrup((char*)m_grupPndtPtrs[0], newPlanetbuff))
            MKFAIL("Could not add new star to existing destination group star record.");
    }

    // Create locations from modified star buffer 
    if (!createLocPlanet(newPlanetbuff, oBasicInfo, newLocbuff))
        MKFAIL("Could not create location record for new star.");

    // TODO: run through all planets with the same Primary and adjust their positions in m_buffer
    // Update planet postions in star system to accomidate new planet
    // TODO will need recompressing data etc
    _adjustPlanetPositions(m_stdts[oBasicInfo.m_iPrimaryIdx], oBasicInfo.m_iPlanetPos); 
    /*
    // Save the locaiton to the location group if it exists otherwise create a new group and add it to that
    if (!m_grupLctnPtrs.size())
    {
        // Create a new group from scratch and add to end of file
        if (!createGrup("LCTN", newLocbuff))
            MKFAIL("Could not add new group record with location.");
    }
    else
    {
        if (!appendToGrup((char*)m_grupLctnPtrs[0], newLocbuff))
            MKFAIL("Could not add new location to existing destination group location record.");
    }*/
   
    return true;
}

    
// Before existing used to check if the data symatics are valid
bool CEsp::checkdata(std::string& strErr)
{
    // TODO: change to report back list of issues
        
    // Check stars have at least one planet
    std::vector<size_t> oStarsWithoutPlanets;
    for (size_t i = 0; i < m_stdts.size(); ++i)
        if (!m_stdts[i].m_isBad)
        {
            std::vector<size_t> oFndPndts;
            if (!findPndtsFromStdt(i, oFndPndts))
                oStarsWithoutPlanets.push_back(i);
        }

    if (oStarsWithoutPlanets.size())
    {
        strErr = "The following star(s) were found without planets: ";
        for (auto& idx : oStarsWithoutPlanets)
            strErr += (m_stdts[idx].m_pAnam->m_aname ? std::string((char *)&m_stdts[idx].m_pAnam->m_aname) : "(no name)") + ", ";
        rlc(strErr);
        return false;
    }

    return true;
}

    
bool CEsp::_saveToFile(const std::vector<char>& newbuff, const std::wstring& wstrfilename, std::string& strErr)
{
    std::ofstream outFile(wstrfilename, std::ios::binary);

    
    if (!outFile)
    {
        strErr = "Failed to open file for writing. " + getFnameAsStr(wstrfilename);
        return false;
    }
    
    if (!outFile.write(newbuff.data(), newbuff.size()))
    {
        outFile.close();
        strErr = "Failed to write to file: " + getFnameAsStr(wstrfilename);
        return false;
    }
    
    outFile.close();
    return true;
}

bool CEsp::copyToBak(std::string &strBakUpName, std::string& strErr)
{
    std::filesystem::path originalFile(m_wstrfilename);

    if (!std::filesystem::exists(originalFile)) 
    {
        strErr = "File " + originalFile.filename().string() + " does not exist: ";
        return false;
    }

    std::filesystem::path bakFilePath = originalFile;
    bakFilePath.replace_extension(bakFilePath.extension().string() + ".bak");

    int counter = 0;
    const int MAX_RENTRY = 300;
    while (std::filesystem::exists(bakFilePath)) 
    {
        if (counter == MAX_RENTRY) // Never have an unguarded loop
        {
            strErr = "Could not rename to " + bakFilePath.filename().string() + " found more than " + std::to_string(MAX_RENTRY) + " .bak files";
            return false;
        }
        ++counter;
        bakFilePath = originalFile;
        bakFilePath.replace_extension(bakFilePath.extension().string() + ".bak" + std::to_string(counter));
    }

    try 
    {
        std::filesystem::copy(originalFile, bakFilePath, std::filesystem::copy_options::overwrite_existing);
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        strErr = "Error making back up file " + bakFilePath.string() + ": " + std::string(e.what());
        return false;
    }

    strBakUpName = bakFilePath.string();
    return true;
} 

bool CEsp::save(std::string &strErr)
{
    strErr.clear();
    // TODO: CopytoBak not used
      
    // TODO: replace with m_wstrfilename, add Save As
    if (!_saveToFile(m_buffer, m_wstrfilename, strErr))
        MKFAIL(strErr);
            
    m_bIsSaved = true;

    // Some how take a copy of the \planetdata\biomemaps (ouch), look into archive, find it and extract it

    return true;
}