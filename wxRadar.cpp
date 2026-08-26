#include "pch.h"
#include "wxRadar.h"

cell wxRadar::wxReturn[256][256];
string wxRadar::wxLatCtr = { "0.0" };
string wxRadar::wxLongCtr = { "0.0" };
int wxRadar::zoomLevel;
string wxRadar::ts;
std::map<string, string> wxRadar::arptAltimeter;
std::map<string, string> wxRadar::arptAtisLetter;
std::vector<CAsyncResponse> wxRadar::asyncMessages;
std::mutex wxRadar::asyncMessagesMutex;
std::shared_mutex wxRadar::altimeterMutex;
std::shared_mutex wxRadar::atisLetterMutex;
json wxRadar::jsVatsimDataFeed;

std::string wxRadar::getSituWxDir()
{
    // GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS with the address of a function in this module
    // gives this DLL's handle rather than the host executable's, so the folder follows the
    // plugin and not EuroScope.exe.
    char modulePath[MAX_PATH] = { 0 };
    HMODULE thisModule = NULL;

    if (GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&wxRadar::getSituWxDir),
        &thisModule) == 0)
    {
        thisModule = NULL;   // falls back to the executable path below
    }

    if (GetModuleFileNameA(thisModule, modulePath, MAX_PATH) == 0)
    {
        // Nothing sensible left to resolve against. The old relative path is still wrong,
        // but it is better than an empty one.
        return ".\\situWx\\";
    }

    std::string dir(modulePath);
    const size_t slash = dir.find_last_of("\\/");

    if (slash == std::string::npos) { return ".\\situWx\\"; }

    return dir.substr(0, slash) + "\\situWx\\";
}

void wxRadar::PushAsyncMessage(const CAsyncResponse& message)
{
    std::lock_guard<std::mutex> lock(asyncMessagesMutex);
    asyncMessages.push_back(message);
}

std::vector<CAsyncResponse> wxRadar::TakeAsyncMessages()
{
    std::vector<CAsyncResponse> taken;
    {
        std::lock_guard<std::mutex> lock(asyncMessagesMutex);
        taken.swap(asyncMessages);
    }
    return taken;
}

void wxRadar::loadPNG(std::vector<unsigned char>& buffer, const std::string& filename) //designed for loading files from hard disk in an std::vector
{
    std::ifstream file(filename.c_str(), std::ios::in | std::ios::binary | std::ios::ate);

    //get filesize
    std::streamsize size = 0;
    if (file.seekg(0, std::ios::end).good()) size = file.tellg();
    if (file.seekg(0, std::ios::beg).good()) size -= file.tellg();

    //read contents of the file into the vector
    if (size > 0)
    {
        buffer.resize((size_t)size);
        file.read((char*)(&buffer[0]), size);
    }
    else buffer.clear();
}

void wxRadar::parseRadarPNG(CRadarScreen* rad) {
    
    GetRainViewerJSON(rad);

    const std::string situWxDir = wxRadar::getSituWxDir();
    const std::string situWxPng = situWxDir + "0_0.png";

    if (CreateDirectoryA(situWxDir.c_str(), NULL)) {}

    // ts is now "<host><path>" from weather-maps.json, so it already carries the scheme,
    // host and /v2/radar/<frame> portion that used to be hardcoded here.
    string tileCacheurl = wxRadar::ts + "/256/4/" + wxRadar::wxLatCtr + "/" + wxRadar::wxLongCtr + "/0/0_0.png";

    const SituHttp::Response tile = SituHttp::Get(tileCacheurl, 10000);

    // A failed fetch used to leave the file truncated to nothing by the "wb" open and
    // then report a parse error against the empty file. Say what actually happened
    // instead, and leave the previous tile on disk.
    if (!tile.ok) {
        rad->GetPlugIn()->DisplayUserMessage("VATCAN Situ", "WX Parser",
            ("Radar tile download failed - " + tile.error).c_str(),
            true, false, false, false, false);
        return;
    }

    // Kept on disk as the visible artifact it has always been, but the decode reads the
    // bytes we already hold rather than writing them out and reading them back.
    FILE* dlPNG = NULL;
    if (fopen_s(&dlPNG, situWxPng.c_str(), "wb") == 0 && dlPNG != NULL) {
        if (!tile.body.empty()) {
            fwrite(tile.body.data(), 1, tile.body.size(), dlPNG);
        }
        fclose(dlPNG);
    }

    std::vector<unsigned char> image;
    const unsigned char* tileBytes = tile.body.empty()
        ? 0
        : reinterpret_cast<const unsigned char*>(tile.body.data());
    unsigned long w, h;
    int error = wxRadar::decodePNG(image, w, h, tileBytes, (unsigned long)tile.body.size());

    // The loop below indexes `image` as a fixed 256x256 RGBA buffer. decodePNG reports the
    // actual dimensions, so validate them: a tile that is not exactly 256x256 (server change,
    // an error page, a truncated download) would otherwise read past the end of the vector.
    const size_t expectedBytes = 256u * 256u * 4u;

    if (error != 0) {
        rad->GetPlugIn()->DisplayUserMessage("VATCAN Situ", "WX Parser", string("PNG Failed to Parse").c_str(), true, false, false, false, false);
    }
    else if (w != 256ul || h != 256ul || image.size() < expectedBytes) {
        rad->GetPlugIn()->DisplayUserMessage("VATCAN Situ", "WX Parser",
            ("Unexpected radar tile size " + to_string(w) + "x" + to_string(h) + " - discarded").c_str(),
            true, false, false, false, false);
    }
    else {

        // convert vector into 2d array with dBa values only;
        // png starts as RGBARGBARGBA... etc. 
        CPosition radReturnTL;

        radReturnTL.m_Longitude = stod(wxRadar::wxLongCtr) - 11.25000;
        radReturnTL.m_Latitude = stod(wxRadar::wxLatCtr);

        // get the pixel coord of the latitude.
        int yCoord = lat2pixel(radReturnTL.m_Latitude, 4);

        // get coord of the top of the image
        yCoord = yCoord - 128;

        for (int i = 0; i < 256; i++) {

            double pixLat = pixel2lat(yCoord + i, 4);
            // calculate latitdue for each row, use the pixel coordinate

            for (int j = 0; j < 256; j++) {

                wxReturn[j][i].dbz = (int)image[(i * 256 * 4) + (j * 4)];
                wxReturn[j][i].cellPos.m_Longitude = radReturnTL.m_Longitude + (double)(j * (22.5 / 256.0));
                wxReturn[j][i].cellPos.m_Latitude = pixLat;
            }
        }
    }
}

int wxRadar::renderRadar(Graphics* g, CRadarScreen* rad, bool showAllPrecip) {

    HatchBrush lightPrecipHatch(HatchStyleDarkUpwardDiagonal, Color(64, 0, 43, 255), Color(0, 0, 0, 0));
    HatchBrush heavyPrecipHatch(HatchStyleDarkUpwardDiagonal, Color(128, 0, 32, 255), Color(0, 0, 0, 0));

    int alldBZ = 60;
    int highdBZ = 80;

    CPosition pos1;
    CPosition pos2;
    CPosition pos3;
    CPosition pos4;

    Point defp1;
    Point defp4;

    bool deferDraw = false;

    // Render radar returns
    for (int i = 0; i < 255; i++) {
        for (int j = 0; j < 255; j++) {

            if (wxReturn[j][i].dbz >= highdBZ || (wxReturn[j][i].dbz >= alldBZ && showAllPrecip)) {

                POINT pix1 = rad->ConvertCoordFromPositionToPixel(wxReturn[j][i].cellPos);
                POINT pix2 = rad->ConvertCoordFromPositionToPixel(wxReturn[j + 1][i].cellPos);
                POINT pix3 = rad->ConvertCoordFromPositionToPixel(wxReturn[j + 1][i + 1].cellPos);
                POINT pix4 = rad->ConvertCoordFromPositionToPixel(wxReturn[j][i + 1].cellPos);

                // draw X for high precip color
                Point p1 = Point(pix1.x, pix1.y);
                Point p2 = Point(pix2.x, pix2.y);
                Point p3 = Point(pix3.x, pix3.y);
                Point p4 = Point(pix4.x, pix4.y);
                Point radarPixel[4] = { p1, p2, p3, p4 };

                if (wxReturn[j][i].dbz >= highdBZ) {
                    // check if next pixel is also true, defer drawing to draw two pixels as one

                    if (j<254 && wxReturn[j+1][i].dbz >= highdBZ && !deferDraw) {

                        deferDraw = true;
                        defp1 = p1;
                        defp4 = p4;

                        continue;
                    }

                    if (deferDraw) {

                        Point defradarPixel[4] = { defp1, p2, p3, defp4 };

                        g->FillPolygon(&heavyPrecipHatch, defradarPixel, 4);
                        deferDraw = false;
                    }
                    else {
                        g->FillPolygon(&heavyPrecipHatch, radarPixel, 4);
                    }
                }

                if (wxReturn[j][i].dbz >= alldBZ && wxReturn[j][i].dbz < highdBZ && showAllPrecip) {
                    if (j < 254 && wxReturn[j+1][i].dbz >= alldBZ && wxReturn[j+1][i].dbz < highdBZ && !deferDraw) {

                        deferDraw = true;
                        defp1 = p1;
                        defp4 = p4;

                        continue;
                    }

                    if (deferDraw) {

                        Point defradarPixel[4] = { defp1, p2, p3, defp4 };

                        g->FillPolygon(&lightPrecipHatch, defradarPixel, 4);
                        deferDraw = false;
                    }
                    else {
                        g->FillPolygon(&lightPrecipHatch, radarPixel, 4);
                    }
                }

            }
        }                       
        
    }
    return 0;   
}

void wxRadar::parseVatsimMetar(int i) {
    
    CAsyncResponse response;

    const SituHttp::Response metar = SituHttp::Get("https://metar.vatsim.net/metar.php?id=c", 2500);
    if (!metar.ok) {
        // Previously only a timeout was reported, so a 500 or a dropped connection went
        // by in silence and the parse below ran on an empty string.
        response.reponseMessage = "METAR fetch failed - " + metar.error;
        response.responseCode = 1;
        wxRadar::PushAsyncMessage(response);
    }
    const string metarString = metar.body;

    altimeterMutex.lock();

    try {
        std::istringstream in(metarString);
        regex altimeterSettingRegex("A[0-9]{4}");
        smatch altimeterSetting;
        string altimeter;

        for (string line; getline(in, line);) {
            string icao = line.substr(0, 4);
            if (regex_search(line, altimeterSetting, altimeterSettingRegex)) {
                altimeter = altimeterSetting[0].str().substr(1, 4);
            }
            else
            {
                altimeter = "****";
            }
            arptAltimeter[icao] = altimeter;
        }
    }
    catch (exception& e) {
        response.reponseMessage = e.what();
        response.responseCode = 1;
        wxRadar::PushAsyncMessage(response);
    }

    altimeterMutex.unlock();
}

void wxRadar::parseVatsimATIS(int i) {
    CAsyncResponse result;

    const SituHttp::Response status = SituHttp::Get("https://status.vatsim.net/status.json", 1500);
    if (!status.ok) {
        result.reponseMessage = "VATSIM datafeed URL fetch failed - " + status.error;
        result.responseCode = 1;
        // This message was built and then never pushed, so the failure was silent.
        PushAsyncMessage(result);
        return;
    }

    string dataURL;

    try {
        json jsVatsimURL = json::parse(status.body);
        dataURL = jsVatsimURL["data"]["v3"][0];
    }
    catch (exception& e) { string error = e.what(); }

    if (dataURL.empty()) {
        result.reponseMessage = "VATSIM datafeed URL missing from status.json";
        result.responseCode = 1;
        PushAsyncMessage(result);
        return;
    }

    const SituHttp::Response atis = SituHttp::Get(dataURL, 1500);
    if (!atis.ok) {
        result.reponseMessage = "VATSIM datafeed failed - ATIS letter may be incorrect ("
            + atis.error + ")";
        result.responseCode = 1;
        PushAsyncMessage(result);
        return;
    }

    const string jsAtis = atis.body;

    {
        // Clear under the lock. This ran outside any lock while the main thread could
        // be reading the same map under a shared_lock in DrawACList, which made the
        // locking below pointless: the writer was mutating it unsynchronised.
        std::unique_lock<shared_mutex> clearLock(atisLetterMutex);
        arptAtisLetter.clear();
    }


    try {
        wxRadar::jsVatsimDataFeed = json::parse(jsAtis.c_str());

        if (!wxRadar::jsVatsimDataFeed["pilots"].empty()) {

            // Build into locals, then publish with a swap under the lock. These maps are
            // read from the main thread in OnFlightPlanFlightPlanDataUpdate; clearing and
            // repopulating them in place from this worker meant the main thread could be
            // looking up a callsign in a map that was mid-rehash.
            std::unordered_map<string, bool> newADSB;
            std::unordered_map<string, bool> newRVSM;

            // make an internal copy of the data feed, but keep it clean for info needed callsign and capabilities
            for (auto& pilot : wxRadar::jsVatsimDataFeed["pilots"]) {
                if (!pilot["flight_plan"]["aircraft"].is_null()) {
                    string icaoACData = pilot["flight_plan"]["aircraft"];
                    regex icaoADSB("(.*)\\/(.*)\\-(.*)\\/(.*)(E|L|B1|B2|U1|U2|V1|V2)(.*)");
                    bool isADSB = regex_search(icaoACData, icaoADSB);

                    regex icaoRVSM("(.*)\\/(.*)\\-(.*)[W](.*)\\/(.*)", regex::icase);
                    bool isRVSM = regex_search(icaoACData, icaoRVSM);

                    newADSB.emplace(std::make_pair(pilot["callsign"], isADSB));
                    newRVSM.emplace(std::make_pair(pilot["callsign"], isRVSM));
                }
            }

            std::unique_lock<shared_mutex> capabilityLock(CSiTRadar::acCapabilityMutex);
            CSiTRadar::acADSB.swap(newADSB);
            CSiTRadar::acRVSM.swap(newRVSM);
        }

        std::unique_lock<shared_mutex> lock(atisLetterMutex);

        if (!jsVatsimDataFeed["atis"].empty()) {
            for (auto& atis : jsVatsimDataFeed["atis"]) {
                if (!atis["atis_code"].is_null()) {
                    string airport = atis["callsign"];
                    arptAtisLetter[airport.substr(0, 4)] = atis["atis_code"];
                }
            }
        }
        lock.unlock();
    }
    catch (exception& e) { result.reponseMessage = e.what(); result.responseCode = 1; PushAsyncMessage(result); return; }

    return;
}