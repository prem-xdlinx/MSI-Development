
#ifndef XDLX_STREAM_H
#define XDLX_STREAM_H

void* _aligned_malloc(size_t size, size_t alignment);
void Stream_callback_func(STREAM_BUFFER_HANDLE streamBufferHandle, void* userContext);
int64_t CaptureImageMap( char* pFrameMemory, size_t bufferSize);

class Stream{
	private:
        STREAM_HANDLE cameraStreamHandle;//[KY_MAX_CAMERAS]; // there are maximum KY_MAX_CAMERAS cameras
        size_t frameDataSize, frameDataAligment;
        STREAM_BUFFER_HANDLE streamBufferHandle[16] = { 0 };
        void *pBuffer[_countof(streamBufferHandle)] = { NULL };
	public:
		//Stream();
        int CreateStreamMap(CAMHANDLE camHandle, int CamID);
        STREAM_HANDLE GetCamstreamHandle();
        void DeleteStream(CAMHANDLE camHandle, int CamID);
        void DeleteStreamMap(CAMHANDLE camHandle, int CamID);
        void saveConfigInfo(CAMHANDLE camHandle);
        void transferFile(const std::string& logPath);
        void saveCommonConfig();
        void freeBuffer();
        bool ProcessData();
        bool writeData();
};

#endif