#include "espmanger.h"

// Read planets PNDT

// Function to decompress the data and store it in the output parameter
bool CEsp::decompress_data(const char* compressed_data, size_t compressed_size, std::vector<char>& decompressed_data, size_t decompressed_size) 
{
    // Initialize the decompressor object
    z_stream inflateStream;
    inflateStream.zalloc = Z_NULL;
    inflateStream.zfree = Z_NULL;
    inflateStream.opaque = Z_NULL;
    inflateStream.avail_in = static_cast<uInt>(compressed_size);
    inflateStream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed_data));

    int ret = inflateInit(&inflateStream);
    if (ret != Z_OK) {
        std::cerr << "Failed to initialize decompression stream." << std::endl;
        return false;
    }

    // Reserve space for the decompressed data based on the provided decompressed_size
    size_t compressed_size__justincase_overflow_buffer = decompressed_size * 3;
    decompressed_data.resize(decompressed_size + compressed_size__justincase_overflow_buffer);

    inflateStream.next_out = reinterpret_cast<Bytef*>(&decompressed_data[0]);
    inflateStream.avail_out = static_cast<uInt>(decompressed_size);

    // Decompress the data
    ret = inflate(&inflateStream, Z_FINISH);
    if (ret != Z_STREAM_END) {
        std::cerr << "Failed to decompress data." << std::endl;
        inflateEnd(&inflateStream);
        return false;
    }

    // Finalize decompression
    inflateEnd(&inflateStream);

    // Resize the vector to the actual decompressed size (in case it's different)
    decompressed_data.resize(decompressed_size - inflateStream.avail_out);

    return true;
}

void CEsp::_buildppbdlist(PNDTrec& oRec, const char*& searchPtr, const char*& endPtr)
{
    size_t taglen = 4;

    #ifdef _DEBUG
    if (oRec.m_pHdr->m_formid == FID_Debug_ClassMPlanet)
    {
        CEsp::no_op(); // debugging
    }
    #endif

    if (BLEFT >= sizeof(PNDTCnamOv))
    { // CNAM records in PNDT
        oRec.m_pCnam = reinterpret_cast<const PNDTCnamOv*>(searchPtr);
        searchPtr += BSKIP(oRec.m_pCnam); // Skip forward
    }

    // These should follow CNAM if they exist
    oRec.m_oPpbds.clear();
    while (BLEFT >= sizeof(PPBDOv) && memcmp(searchPtr, "PPBD", taglen) == 0 && oRec.m_oPpbds.size() < MAXPPBD) // don't loop forever if there is an problem
    {
        const PPBDOv* pPpbd = nullptr;
        pPpbd = reinterpret_cast<const PPBDOv*>(searchPtr);
        oRec.m_oPpbds.push_back(pPpbd);
        searchPtr += BSKIP(pPpbd); // Skip forward
    }
}

// build list of strings are in the middle of the PNDT HNAM 
void CEsp::_buildHnamRec(PNDTHnam2Rec& oHnam2, const char*& searchPtr, const char*& endPtr)
{
    for (size_t i = 0; i < NUMHNAMSTRINGS && searchPtr < endPtr; i++)
        if (BLEFT >= sizeof(TES4LongStringOv))
            _readLongString(oHnam2.m_Strings[i], searchPtr, endPtr);
}

void CEsp::_dopndt_op_findparts(PNDTrec& oRec, const char*& searchPtr, const char*& endPtr)
{
    bool bFndBdst = false;
    size_t taglen = 4;
    oRec.m_pEdid = &BADEDIDREC;
    oRec.m_pAnam = &BADPNDTANAMREC;
    oRec.m_pGnam = &BADPNDTGNAMREC;
    oRec.m_pHnam1 = &BADPNDTHNAMREC1;
    oRec.m_pHnam3 = &BADPNDTHNAMREC3;
    oRec.m_isBad = true;
    oRec.m_isMissingMatchingBfce = false;
    oRec.m_oComp.clear();

    // Find the EDID
    // TODO template This kind of operation
    while (searchPtr < endPtr)
    {
        if (BLEFT >= taglen)
        { 
            if (memcmp(searchPtr, "EDID", taglen) == 0) 
            {
                if (BLEFT >= sizeof(EDIDrecOv))
                {
                    oRec.m_pEdid = reinterpret_cast<const EDIDrecOv*>(searchPtr);
                    searchPtr +=  BSKIP(oRec.m_pEdid); // Skip forward
                    break;
                }
            }
        }
        searchPtr++;
    }

    //  Find BDST tag first as ANAM and GNAM can appear in mutliple places, we need ones in BDST section
    while (searchPtr < endPtr)
    {
        if (BLEFT >= taglen)
        { 
            if (memcmp(searchPtr, "BFCB", taglen) == 0)
            {
                if (BLEFT >= sizeof(BFCBrecOv) + sizeof(BFCBDatarecOv))
                {
                    #ifdef _DEBUG
                    if (oRec.m_pHdr->m_formid == FID_Debug_ClassMPlanet)
                    {
                        CEsp::no_op(); // debugging
                    }
                    #endif

                    oRec.m_isMissingMatchingBfce = _readBFCBtoBFBE(searchPtr, endPtr, oRec.m_oComp);
                    continue; 
                }
            }
            else
            if (memcmp(searchPtr, "CNAM", taglen) == 0)
            {
                // Oddly the CNAM can appear after a BFCE and before a BDST or a ANAM - go figure
                _buildppbdlist(oRec, searchPtr, endPtr);
            }
            else
            if (memcmp(searchPtr, "BDST", taglen) == 0)
            {
                bFndBdst = true;
                break; // Done
            }
        }
        searchPtr++;
    }

    if (bFndBdst) // If did not find it, then it's a bad record, don't do more work
    {
        while (searchPtr < endPtr)
        {
            if (BLEFT >= taglen)
            {
                if (memcmp(searchPtr, "ANAM", taglen) == 0)
                {
                    if (BLEFT >= sizeof(PNDTAnamOv))
                    {
                        oRec.m_pAnam = reinterpret_cast<const PNDTAnamOv*>(searchPtr);
                        searchPtr += BSKIP(oRec.m_pAnam); // Skip forward
                    }
                    continue;
                }
                else
                if (memcmp(searchPtr, "CNAM", taglen) == 0)
                {
                    // Ignore ones in this section for now
                    // _buildppbdlist(oRec, searchPtr, endPtr);
                }
                else
                if (memcmp(searchPtr, "GNAM", taglen) == 0)
                {
                    if (BLEFT >= sizeof(PNDTGnamOv))
                    {
                        oRec.m_pGnam = reinterpret_cast<const PNDTGnamOv*>(searchPtr);
                        searchPtr += BSKIP(oRec.m_pGnam); // Skip forward
                    }
                    continue;
                }
                if (memcmp(searchPtr, "HNAM", taglen) == 0)
                {
                    if (BLEFT >= sizeof(PNDTHnam1Ov))
                    {
                        oRec.m_pHnam1 = reinterpret_cast<const PNDTHnam1Ov*>(searchPtr);
                        searchPtr += sizeof(PNDTHnam1Ov); // Skip forward

                        _buildHnamRec(oRec.m_oHnam2, searchPtr, endPtr);

                        if (BLEFT >= sizeof(PNDTHnam3Ov))
                        {
                            oRec.m_pHnam3 = reinterpret_cast<const PNDTHnam3Ov*>(searchPtr);
                            searchPtr += sizeof(PNDTHnam3Ov); // Skip forward
                        }
                    }
                    break; // Done - Not looking for anything beyond this
                }
            }
            searchPtr++;
        }
    }

    // check if record okay
    if (oRec.m_pEdid->m_size && oRec.m_pEdid->m_name && oRec.m_pAnam->m_size && oRec.m_pAnam->m_aname && oRec.m_pGnam->m_size)
        oRec.m_isBad = false;
}

// Do all the work to build out a PNDT record data structure
// It is compressed so decompressing is most of the effort
void CEsp::_dopndt_op(size_t iPndtIdx)
{
    PNDTrec& oRec = m_pndts[iPndtIdx];
    decompress_data(oRec.m_pcompdata, oRec.m_compdatasize, oRec.m_decompdata, oRec.m_pHdr->m_decompsize);

    // TODO: build out other PNDT data records
    const char* searchPtr = &oRec.m_decompdata[0];
    const char* endPtr = &oRec.m_decompdata[oRec.m_decompdata.size() - 1];

    _dopndt_op_findparts(oRec, searchPtr, endPtr);
    if (oRec.m_isMissingMatchingBfce)
    {
        std::lock_guard<std::mutex> guard(m_output_mutex);
        m_MissingBfceMap[oRec.m_pHdr->m_formid] = GENrec(eESP_PNDT, iPndtIdx);
    }

    if (oRec.m_isBad)
    {
        // Add to bad list
        std::lock_guard<std::mutex> guard(m_output_mutex);
        m_BadMap[oRec.m_pHdr->m_formid] = GENrec(eESP_PNDT, iPndtIdx);
    }
    else
    {
        // Add to star system id index
        std::lock_guard<std::mutex> guard(m_output_mutex);
        m_SystemIDMap[oRec.m_pGnam->m_systemId].push_back(GENrec(eESP_PNDT, iPndtIdx));
    }
}

void CEsp::process_pndt_ranged_op_mt(size_t start, size_t end)
{
    for (size_t i = start; i < end; ++i)
        _dopndt_op(i);
}

// multi-tread process for getting PNDT data from compressed streams
void CEsp::dopndt_op_mt()
{
    size_t num_pointers = m_pndts.size();
    if (num_pointers == 0) return; // No work to do if no pointers
    size_t num_threads = 0;
    size_t chunk_size = 0;

    chunk_size = getmtclunks(num_pointers, num_threads);
    std::vector<std::future<void>> futures;

    for (size_t i = 0; i < num_threads; ++i)
    {
        size_t start = i * chunk_size;
        size_t end = std::min(start + chunk_size, num_pointers);

        // Ensure start < end
        if (start < end)
            futures.push_back(std::async(std::launch::async, &CEsp::process_pndt_ranged_op_mt, this, start, end));
    }

    // Wait for all threads to complete
    for (auto& future : futures)
        future.get();
}
