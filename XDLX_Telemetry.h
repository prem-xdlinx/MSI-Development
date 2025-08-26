#ifndef XDLX_TELEMETRY_H
#define XDLX_TELEMETRY_H


class Telemetry {
public:
    Telemetry();
    ~Telemetry();
    bool initialize(FGHANDLE fgHandle, CAMHANDLE CamHandle);
    void fetchDeviceStatus(FGHANDLE fgHandle, CAMHANDLE CamHandle);
    void terminate();
};

#endif // XDLX_TELEMETRY_