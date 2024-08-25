#include "espmanger.h"

// write operations to save items

template <typename T>
T* CEsp::makeMutable(const T* ptr)
{
    T* mutablePtr = const_cast<T*>(ptr);
    if (!mutablePtr)
        throw std::runtime_error("Null pointer cannot be made mutable.");
    return mutablePtr;
}

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

    HEDRHdrOv * pMutableHdr = makeMutable(getHEDRHdr_ptr()); 
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

// remove spacce in buffer between removestart and remove end, include start, excluding end
void CEsp::_deleterec(std::vector<char>& newbuff, const char* removestart, const char* removeend)
{
    auto start = newbuff.begin() + (removestart - &newbuff[0]);
    auto end = newbuff.begin() + (removeend - &newbuff[0]);

    // Ensure that start is before end
    if (start < end && start >= newbuff.begin() && end <= newbuff.end())
        newbuff.erase(start, end);
}

// insert a string into the block of memory over existing string 
void CEsp::_insertbuff(std::vector<char>& newbuff, char* pDstInsertPosition, size_t oldSize, const char* pNewbuff, size_t iSizeNewBuffer)
{
    if (!newbuff.size() || !pDstInsertPosition || !pNewbuff || !iSizeNewBuffer) 
        return;

    {   // points inside {} will be invalid after insert/erase operations below
        size_t newSize = iSizeNewBuffer; 

        // Calculate the position of the destination name within the buffer
        ptrdiff_t offset = pDstInsertPosition - newbuff.data();

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
    }
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


// Creates a location record
CEsp::formid_t CEsp::_createLoc(std::vector<char> &newLocbuff, const char* szLocName, const char* szOrbName, formid_t parentid, formid_t systemid, 
    uint32_t *pkeywords, size_t numkeywords, size_t faction, size_t iLvlMin, size_t iLvlMax,  size_t iPlanetPosNum)
{
    if (iPlanetPosNum>0xFF || iLvlMax>0xFF || iLvlMin>0xFF || !szLocName || !*szLocName || !szOrbName || !*szOrbName)
        return false; // bad params

    uint16_t taglen = 4;
    size_t totalsize = 0;
    formid_t LocFormId = 0;
    size_t istartsize = newLocbuff.size();

    // Some macros to prevent typos
    #define _setcomm(x, y) memcpy(&x, y, taglen); x.m_size = sizeof(x) - (taglen + sizeof(x.m_size))
    #define _setcommhdr(x, y) memcpy(&x, y, taglen); x.m_size = 0
    #define _paddrecsize(x) (x->m_size + taglen + sizeof(x->m_size))

    // { } used to prevent typoes from cut/paste by enforcing scope
    {// LCTN header
        LCTNHdrOv ohdr;
        _setcommhdr(ohdr, "LCTN"); // hdr size excludes size of hdr record
        LocFormId = ohdr.m_formid = _createNewFormId();
        totalsize = ohdr.m_size;
        _addtobuff(newLocbuff, &ohdr, sizeof(ohdr));
    }

    // Note: _makegenblock also updates the m_size for the pasted record pointer, it assumes its after the taglen and is 2bytes

    {// EDID Editor id - name
        uint32_t size = 0;
        EDIDrecOv* pEdid = reinterpret_cast<EDIDrecOv*>(_makegenblock("EDID", std::string(szLocName).c_str()));
        totalsize += size = _paddrecsize(pEdid);
        _addtobuff(newLocbuff, pEdid, size);
    }

    {// FULL full name for location
        uint32_t size = 0;
        FULLrecOv* pFull = reinterpret_cast<FULLrecOv*>(_makegenblock("FULL", szOrbName));
        totalsize += size = _paddrecsize(pFull);
        _addtobuff(newLocbuff, pFull, size);
    }

    {// KSIZ Keywords how many records   
        KSIZrecOv oKsiz;
        _setcomm(oKsiz, "KSIZ");
        oKsiz.m_count = static_cast<uint32_t>(numkeywords);
        totalsize += sizeof(oKsiz);
        _addtobuff(newLocbuff, &oKsiz, sizeof(oKsiz));
    }

    {// KWDA Keyword(s) data - one keyword LocTypeStarSystem
        uint32_t size = 0;
        KWDArecOv* pKwda = reinterpret_cast<KWDArecOv*>(_makegenblock("KWDA", pkeywords, sizeof(uint32_t)*numkeywords));
        totalsize += size = _paddrecsize(pKwda);
        _addtobuff(newLocbuff, pKwda, size);
    }
        
    { // DATA data record of location
        LCTNDataOv oData;
        _setcomm(oData, "DATA");
        oData.m_faction = static_cast<formid_t>(faction);
        oData.m_playerLvl = static_cast<uint8_t>(iLvlMin);
        oData.m_playerLvlMax = static_cast<uint8_t>(iLvlMax);
        totalsize += sizeof(oData);
        _addtobuff(newLocbuff, &oData, sizeof(oData));
    }

    {// PNAM Parent of the star - Universe
        LCTNPnamOv oPnam;
        _setcomm(oPnam, "PNAM");
        oPnam.m_parentloc = parentid;
        totalsize += sizeof(oPnam);
        _addtobuff(newLocbuff, &oPnam, sizeof(oPnam));
    }

    { // XNAM Star system id
        LCTNXnamOv oXnam;
        _setcomm(oXnam, "XNAM");
        oXnam.m_systemId = systemid;
        totalsize += sizeof(oXnam);
        _addtobuff(newLocbuff, &oXnam, sizeof(oXnam));
    }

    { // YNAM Planet position in system, set to zero for stars - don't know why this has to be in the Loc record
        LCTNYnamOv oYnam;
        _setcomm(oYnam, "YNAM");
        oYnam.m_planetpos = static_cast<uint32_t>(iPlanetPosNum);
        totalsize += sizeof(oYnam);
        _addtobuff(newLocbuff, &oYnam, sizeof(oYnam));
    }

    // put the new size into the buffer hdr
    // +istartsize because buffer might not be empty when we start it could have a LTCN record already in it and we are adding to it
    if (newLocbuff.size() > istartsize)
    {
        LCTNHdrOv* pHdr = reinterpret_cast<LCTNHdrOv*>(newLocbuff.data() + istartsize);
        pHdr->m_size = static_cast<uint32_t>(totalsize);
    }
    else
        return 0; // the buffer did not grow during the process of building the LTCN. Bug.

    return LocFormId;
}

// Sets the record size to the size of the memory buffer, minus the header size
bool CEsp::_refreshsizeStdt(std::vector<char>& newbuff, const STDTrec& oRec)
{
    STDTHdrOv* pMutableHdr = makeMutable(oRec.m_pHdr);  // recalc ptr due to mem changes
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
void CEsp::_clonefixupcompsStdt(std::vector<char>& newbuff, CEsp::STDTrec& oRec)
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
CEsp::formid_t CEsp::createLocStar(const std::vector<char> &newStarbuff, const BasicInfoRec &oBasicInfo, std::vector<char> &newLocbuff)
{
    uint16_t taglen = 4;
    size_t totalsize = 0;

    // Create an oRec from the passed star buffer so we can easily get info from it
    STDTrec oRec = {};
    _rebuildStdtRecFromBuffer(oRec, newStarbuff);
    if (!oRec.m_pAnam)
        return 0;

    const char* pAname = reinterpret_cast<const char*>(&oRec.m_pAnam->m_aname);
    if (!pAname)
        return 0; 

    std::string strStarName = std::string(pAname);
    std::string strLocName ="S" + strStarName;
    uint32_t keywords[] = { KW_LocTypeStarSystem }; 

    formid_t StarLocId =  _createLoc(newLocbuff, 
        strLocName.c_str(), 
        strStarName.c_str(),
        FID_Universe,  
        oRec.m_pDnam->m_systemId,
        &keywords[0], sizeof(keywords)/sizeof(uint32_t), 
        oBasicInfo.m_iFaction, 
        oBasicInfo.m_iSysPlayerLvl, oBasicInfo.m_iSysPlayerLvlMax, 0);

    return StarLocId;
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

    STDTHdrOv * pMutableHdr = makeMutable(oRec.m_pHdr); 
    pMutableHdr->m_formid = _createNewFormId();
    if (!pMutableHdr->m_formid)
        return false; // id should not be zero, possible bad record

    if (!oRec.m_pBnam || !oRec.m_pDnam)
        return false; // Bad position pointer, or bad System id pointer

    STDTBnamOv* pMutableBnam = makeMutable(oRec.m_pBnam); 
    pMutableBnam->m_xPos = oBasicInfo.m_StarMapPostion.m_xPos;
    pMutableBnam->m_yPos = oBasicInfo.m_StarMapPostion.m_yPos;
    pMutableBnam->m_zPos = oBasicInfo.m_StarMapPostion.m_zPos;

    STDTDnamOv * pMutableDnam = makeMutable(oRec.m_pDnam); 
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
bool CEsp::makestar(const CEsp *pSrc, size_t iSrcStarIdx, const BasicInfoRec &oBasicInfo, std::string &strErr)
{
    std::vector<char> newStarbuff;

    if (!pSrc)
        MKFAIL("No source provided to create the star from.");

    // Check values make sense, since we will be casting these down to uint8
    if (oBasicInfo.m_iSysPlayerLvl > 0xFF || oBasicInfo.m_iSysPlayerLvlMax > 0xFF || oBasicInfo.m_iPlanetPos >0xFF)
        MKFAIL("Could not create star as the values Player Level, or Planet Positon are larger than 255.");

    // Create and save into m_buffer the new star
    try 
    {
        if (!cloneStdt(newStarbuff, pSrc->m_stdts[iSrcStarIdx], oBasicInfo)) // can throw if found nullptr
            MKFAIL("Could not clone selected star.");
    } 
    catch (const std::runtime_error& e)
    {
        MKFAIL(e.what());
    }


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
    std::vector<char> newLocbuff;
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
bool CEsp::createLocPlanet(const std::vector<char>& newPlanetbuff, const BasicInfoRec& oBasicInfo, std::vector<char>& newLocbuff)
{
    uint16_t taglen = 4;
    size_t totalsize = 0;

    // Create an oRec from the passed star buffer so we can easily get info from it
    PNDTrec oRec = {};
    oRec.m_pHdr = reinterpret_cast<const PNDTHdrOv*>(&newPlanetbuff[0]);
    _decompressPndt(oRec, newPlanetbuff);
    _rebuildPndtRecFromBuffer(oRec, newPlanetbuff);

    if (!oRec.m_pGnam || !oRec.m_pAnam || !oRec.m_pHdr)
        return false; // Did not build fully the oRec from the decompressed buffer 

    // get info for location creation
    formid_t starId = m_stdts[oBasicInfo.m_iPrimaryIdx].m_pHdr->m_formid;
    std::string strStarName = (char*)&(m_stdts[oBasicInfo.m_iPrimaryIdx].m_pAnam->m_aname); 
    formid_t systemId = oRec.m_pGnam->m_systemId;

    formid_t StarLocId = 0;
    // Find the locaiton record for the star so we reference it from the planet location
    // TODO: use  map perhaps, but there won't be many loc records in the esp (expecting <100)
    for (auto& oLctn : m_lctns)
        if (oLctn.m_pXnam->m_systemId == oRec.m_pGnam->m_systemId)
            StarLocId = oLctn.m_pHdr->m_formid;

    if (!StarLocId)
        return false; // could not find location record for the star the planet needs to be under

    const char* pAname = reinterpret_cast<const char*>(&oRec.m_pAnam->m_aname);
    std::string strPlanetName = std::string(pAname);
    std::string strLocName =  "S" + strStarName + "_P" + strPlanetName;

    // TODO set the corect keywords
    uint32_t keywordsPlanet[] = { KW_LocTypePlanet,  KW_LocTypeMajorOribital }; 
    uint32_t keywordsOrbit[] = { KW_LoctTypeOrbit }; 
    uint32_t keywordsSurface[] = { KW_LoctTypeSurface }; 

    // Planet location record
    formid_t planetLocId = 0;
    if (!(planetLocId = _createLoc(newLocbuff, strLocName.c_str(), strPlanetName.c_str(), StarLocId, systemId,
        &keywordsPlanet[0], sizeof(keywordsPlanet) / sizeof(uint32_t),
        oBasicInfo.m_iFaction, oBasicInfo.m_iSysPlayerLvl, oBasicInfo.m_iSysPlayerLvlMax, oBasicInfo.m_iPlanetPos)))
        return false;

    // Create orbit reference
    if (!_createLoc(newLocbuff, std::string(strLocName + "_Orbit").c_str(), strPlanetName.c_str(), planetLocId, systemId,
        &keywordsOrbit[0], sizeof(keywordsOrbit) / sizeof(uint32_t),
        oBasicInfo.m_iFaction, oBasicInfo.m_iSysPlayerLvl, oBasicInfo.m_iSysPlayerLvlMax, oBasicInfo.m_iPlanetPos))
        return false;

    // Create surface reference
    if (!_createLoc(newLocbuff, std::string(strLocName + "_Surface").c_str(), strPlanetName.c_str(), planetLocId, systemId,
        &keywordsSurface[0], sizeof(keywordsSurface) / sizeof(uint32_t),
        oBasicInfo.m_iFaction, oBasicInfo.m_iSysPlayerLvl, oBasicInfo.m_iSysPlayerLvlMax, oBasicInfo.m_iPlanetPos))
        return false;

    return true;
}

// Sets the record size to the size of the memory buffer, minus the header size
bool CEsp::_refreshHdrSizesPndt(const PNDTrec& oRec, size_t decomsize)
{
    PNDTHdrOv* pMutableHdr = makeMutable(oRec.m_pHdr);  // recalc ptr due to mem changes
    pMutableHdr->m_size = oRec.m_compdatasize + sizeof(oRec.m_pHdr->m_decompsize); 
    pMutableHdr->m_decompsize = static_cast<uint32_t>(decomsize);

    return true;
}

// Given the buffer to a complete pndt record decompress it into the oRec decomp buffer and update the sizes
void CEsp::_decompressPndt(PNDTrec& oRec, const std::vector<char>& newpndbuff)
{
    oRec.m_pcompdata = &newpndbuff[sizeof(PNDTHdrOv)];
    oRec.m_compdatasize = oRec.m_pHdr->m_size - sizeof(uint32_t);  // size in hdr - compressed data + the 4 bytes that hold the decomp size
    decompress_data(oRec.m_pcompdata, oRec.m_compdatasize, oRec.m_decompdata, oRec.m_pHdr->m_decompsize);
}

void CEsp::_rebuildPndtRecFromBuffer(PNDTrec &oRec, const std::vector<char>&newpndbuff)
{
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

#ifdef Never
    // example code for how to remove bioms
    // Remove Bioms
    int i = 0; std::string strErr;
    while (oRec.m_oPpbds.size())
    {
        const PPBDOv* pPpbd = oRec.m_oPpbds.back();
        if (!pPpbd)
            break;
        oRec.m_oPpbds.pop_back();

        const char* removestart = reinterpret_cast<const char*>(pPpbd);
        const char* removeend = &removestart[pPpbd->m_size + sizeof(pPpbd->m_PPBDtag) + sizeof(pPpbd->m_size)];
         _deleterec(oRec.m_decompdata, removestart, removeend);
        _rebuildPndtRecFromBuffer(oRec, oRec.m_decompdata); // reload info due to memory changes
        _refreshHdrSizesPndt(oRec, oRec.m_decompdata.size()); // refresh sizes affected by remove
        // debug _saveToFile(oRec.m_decompdata, L"F:\\downloads\\cut" + std::to_wstring(i++) + L".bin", strErr);
    }
#endif

// need to fix up component records which where cloned and came from an ESM
void CEsp::_clonefixupcompsPndt(CEsp::PNDTrec& oRec)
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
                _insertname(oRec.m_decompdata, (char *)&pFULLrecOv->m_name, (uint16_t *)&pFULLrecOv->m_size, (char *)&oRec.m_pAnam->m_aname);
                _rebuildPndtRecFromBuffer(oRec, oRec.m_decompdata); // reload info due to memory changes
                _refreshHdrSizesPndt(oRec, oRec.m_decompdata.size()); // refresh sizes affected by insert
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

// Make a clone of the passed record in ostdRec, chnaging the clone with the info oBasicInfo
// put the result in newbuff
bool CEsp::clonePndt(std::vector<char> &newbuff, const PNDTrec &opndtRec, const BasicInfoRec &oBasicInfo)
{
    // TODO Clean up all the PTR stuff
    if (!m_buffer.size() || !*oBasicInfo.m_pName || opndtRec.m_isBad || !opndtRec.m_pEdid->m_name)
        return false;

    // make newbuff the size of the STDT specified in the hdr and copy it over from the source
    // Note unlike GRUP records, the size of the record does not include the size of the STDT header 

    size_t sizeofpndntoclone = (opndtRec.m_pHdr->m_size + sizeof(PNDTHdrOv))-sizeof(opndtRec.m_pHdr->m_decompsize); 
    newbuff.resize(sizeofpndntoclone);
    memcpy(&newbuff[0], &opndtRec.m_pHdr[0], sizeofpndntoclone);

    // Build an PNDTrec so it will be easier to patch up the names etc
    PNDTrec oRec = {};
     oRec.m_pHdr = reinterpret_cast<const PNDTHdrOv*>(&newbuff[0]);
     _decompressPndt(oRec, newbuff);
    _rebuildPndtRecFromBuffer(oRec, newbuff);
    if (!oRec.m_pHdr)
        return false; // should not happen bad hdr ptr

    // Note for planets, pointers in oRec point into the decompressed buffer

    // For time being override const, alot of reworking required to make this clear which could get through away if more OV added
    PNDTHdrOv * pMutableHdr = makeMutable(oRec.m_pHdr); 
    pMutableHdr->m_formid = _createNewFormId();
    if (!pMutableHdr->m_formid)
        return false; // id should not be zero, possible bad record

    // Do some validation because a change is better than a rest 
    if (oBasicInfo.m_iPrimaryIdx > m_stdts.size() || !m_stdts[oBasicInfo.m_iPrimaryIdx].m_pDnam->m_systemId || 
        !m_stdts[oBasicInfo.m_iPrimaryIdx].m_pDnam)
        return false; // bad primary index

    // Put the planet in the star system of the star indx in PirmaryIdx
    PNDTGnamOv * pMutableGnam = makeMutable(oRec.m_pGnam); 
    pMutableGnam->m_systemId = m_stdts[oBasicInfo.m_iPrimaryIdx].m_pDnam->m_systemId;
    pMutableGnam->m_primePndtId = static_cast<uint32_t>(oBasicInfo.m_iPlanetPos); 
    
    if (!oRec.m_pEdid || !oRec.m_pEdid->m_size || !oRec.m_pEdid->m_name)
        return false; // bad editor name

    // Do mods to the decompressed buffer which affect the size.
    _insertname(oRec.m_decompdata, (char *)&oRec.m_pEdid->m_name, (uint16_t *)&oRec.m_pEdid->m_size, oBasicInfo.m_pName);
    _rebuildPndtRecFromBuffer(oRec, oRec.m_decompdata); // reload info due to memory changes
    _refreshHdrSizesPndt(oRec, oRec.m_decompdata.size()); // refresh sizes affected by insert
   
    if (!oRec.m_pAnam || !oRec.m_pAnam->m_size || !oRec.m_pAnam->m_aname)
        return false; // Bad full name

    _insertname(oRec.m_decompdata, (char *)&oRec.m_pAnam->m_aname, (uint16_t *)&oRec.m_pAnam->m_size, oBasicInfo.m_pAName);
    _rebuildPndtRecFromBuffer(oRec, oRec.m_decompdata); // reload info due to memory changes
    _refreshHdrSizesPndt(oRec, oRec.m_decompdata.size()); // refresh sizes affected by insert

    // Remove POIs, delete contents of CNAM and set its size to zero but keep tag and the size of 0x0000
    if (oRec.m_pCnam)
    {
        const char* removestart = (reinterpret_cast<const char*>(oRec.m_pCnam)) + sizeof(oRec.m_pCnam->m_CNAMtag) + sizeof(oRec.m_pCnam->m_size);
        const char* removeend = &removestart[oRec.m_pCnam->m_size];
        PNDTCnamOv * pMutableHdr = makeMutable(oRec.m_pCnam); 
        pMutableHdr->m_size = 0;
        _deleterec(oRec.m_decompdata, removestart, removeend);
        _rebuildPndtRecFromBuffer(oRec, oRec.m_decompdata); // reload info due to memory changes
        _refreshHdrSizesPndt(oRec, oRec.m_decompdata.size()); // refresh sizes affected by remove
    }
 
    // Another strings to replace?

    // FNAM surface tree?
    // need the biome exported for landing to work
    // strings in houdini data see fn below?
    // planet position not resolved
        
    // Ouch, Now fix up some records which don't work in an ESP if taken from an ESM
    _clonefixupcompsPndt(oRec);

    // Debug will dump the uncompressed data to default downloads directory
    #ifdef _DEBUG
    if (IsDebuggerPresent())
        _debugDumpVector(oRec.m_decompdata, oBasicInfo.m_pName);
    #endif

    // Recompress the compressed buffer in a temp copy then insert it overtop the current compressed buffer
    std::vector<char> newcompressbuffer;
    if (!compress_data(oRec.m_decompdata.data(), oRec.m_pHdr->m_decompsize, newcompressbuffer))
        return false;

    _insertbuff(newbuff, (char *)oRec.m_pcompdata, oRec.m_compdatasize, newcompressbuffer.data(), newcompressbuffer.size());
    oRec.m_pHdr = reinterpret_cast<const PNDTHdrOv*>(&newbuff[0]); // reset the hdr ptr in case address of new buff moved
    _rebuildPndtRecFromBuffer(oRec, newbuff); // reload info due to memory changes

    oRec.m_compdatasize = static_cast<uint32_t>(newcompressbuffer.size());
    pMutableHdr = makeMutable(oRec.m_pHdr); 
    pMutableHdr->m_size = static_cast<uint32_t>(newcompressbuffer.size() + sizeof(uint32_t));

    return true;
}

bool CEsp::makeplanet(const CEsp* pSrc, size_t iSrcPlanetIdx, const BasicInfoRec& oBasicInfo, std::string& strErr)
{
    strErr.clear();
    std::vector<char> newPlanetbuff;

    if (!pSrc)
        MKFAIL("No source provided to create the planet from.");

    // Check values make sense, since we will be casting these down to uint8
    if (oBasicInfo.m_iSysPlayerLvl > 0xFF || oBasicInfo.m_iSysPlayerLvlMax > 0xFF || oBasicInfo.m_iPlanetPos >0xFF)
        MKFAIL("Could not create planet as the values Player Level, or Planet Positon are larger than 255.");

    // Create and save into m_buffer the new star
    try 
    {
        if (!clonePndt(newPlanetbuff, pSrc->m_pndts[iSrcPlanetIdx], oBasicInfo)) // can throw if found nullptr
            MKFAIL("Could not clone selected planet.");
    } 
    catch (const std::runtime_error& e)
    {
        MKFAIL(e.what());
    }

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
    std::vector<char> newLocbuff;
    if (!createLocPlanet(newPlanetbuff, oBasicInfo, newLocbuff))
        MKFAIL("Could not create location record for new star.");

    // Update planet postions in star system to accomidate new planet
    _adjustPlanetPositions(m_stdts[oBasicInfo.m_iPrimaryIdx], oBasicInfo.m_iPlanetPos); 
    
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
    // Extract the directory path from the full file path
    std::filesystem::path filePath(wstrfilename);
    std::filesystem::path directoryPath = filePath.parent_path();

    // Create the directory structure if it doesn't exist
    try 
    {
        if (!directoryPath.empty() && !std::filesystem::exists(directoryPath)) 
            std::filesystem::create_directories(directoryPath);
    } 
    catch (const std::filesystem::filesystem_error& e) 
    {
        strErr = "Failed to create directories for file: " + std::string(e.what());
        return false;
    }

    // Now open the file and write to it
    std::ofstream outFile(wstrfilename, std::ios::binary);
    
    if (!outFile)
    {
        strErr = "Failed to open file for writing: " + getFnameAsStr(wstrfilename);
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
    if (!_saveToFile(m_buffer, m_wstrfilename, strErr))
        MKFAIL(strErr);
            
    m_bIsSaved = true;

    // Some how take a copy of the \planetdata\biomemaps (ouch), look into archive, find it and extract it

    return true;
}