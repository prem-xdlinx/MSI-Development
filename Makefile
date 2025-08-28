# 
# # Default include/lib paths for KAYA installation
# KAYA_INC_PATH ?= "/opt/KAYA_Instruments/include"
# KAYA_LIB_PATH ?= "/opt/KAYA_Instruments/lib"

# LDFLAGS += -L${KAYA_LIB_PATH}
# LDFLAGS += -Wl,-rpath,${KAYA_LIB_PATH}
# LDFLAGS += -Wl,-rpath,'$$ORIGIN'

# KYFLAGS +=  # add -lz here if needed

# # Include Antaris payload env
# include /lib/antaris/include/Makefile.inc

# # Object files from original project
# KAYA_OBJS = XDLX_CamApp_V6.o XDLX_Kaya_UI.o XDLX_Kaya_Grabber.o XDLX_Camera.o XDLX_Stream.o XDLX_Pack.o XDLX_Telemetry.o

# # Targets
# all: payload_app

# #########################################
# # Compile object files (first project)
# #########################################

# XDLX_CamApp_V6.o: GenericParam.h XDLX_Kaya_UI.h sharedParams.h XDLX_CamApp_V6.cpp 
# 	c++ -c XDLX_CamApp_V6.cpp -I$(KAYA_INC_PATH) -I /usr/local/include/ -I /usr/lib/antaris/include -I /usr/lib/antaris/gen $(LDFLAGS) -lKYFGLib $(KYFLAGS) $(LDFLAGS) -lKYFGLib $(KYFLAGS)

# XDLX_Kaya_UI.o: GenericParam.h XDLX_Kaya_UI.h sharedParams.h XDLX_Kaya_UI.cpp 
# 	c++ -c XDLX_Kaya_UI.cpp -I$(KAYA_INC_PATH) -I /usr/local/include/ -I /usr/lib/antaris/include -I /usr/lib/antaris/gen $(LDFLAGS) -lKYFGLib $(KYFLAGS) $(LDFLAGS) -lKYFGLib $(KYFLAGS)

# XDLX_Kaya_Grabber.o: GenericParam.h XDLX_Kaya_Grabber.h sharedParams.h XDLX_Kaya_Grabber.cpp 
# 	c++ -c XDLX_Kaya_Grabber.cpp -I$(KAYA_INC_PATH) -I /usr/local/include/ -I /usr/lib/antaris/include -I /usr/lib/antaris/gen $(LDFLAGS) -lKYFGLib $(KYFLAGS) $(LDFLAGS) -lKYFGLib $(KYFLAGS)

# XDLX_Camera.o: GenericParam.h XDLX_Camera.h sharedParams.h XDLX_Camera.cpp 
# 	c++ -c XDLX_Camera.cpp -I$(KAYA_INC_PATH) -I /usr/local/include/ -I /usr/lib/antaris/include -I /usr/lib/antaris/gen $(LDFLAGS) -lKYFGLib $(KYFLAGS) $(LDFLAGS) -lKYFGLib $(KYFLAGS)

# XDLX_Stream.o: GenericParam.h XDLX_Stream.h sharedParams.h XDLX_Stream.cpp 
# 	c++ -c XDLX_Stream.cpp -I$(KAYA_INC_PATH) -I /usr/local/include/ -I /usr/lib/antaris/include -I /usr/lib/antaris/gen $(LDFLAGS) -lKYFGLib $(KYFLAGS) $(LDFLAGS) -lKYFGLib $(KYFLAGS) 

# XDLX_Telemetry.o :  GenericParam.h XDLX_Telemetry.h sharedParams.h XDLX_Telemetry.cpp 
# 	c++ -c XDLX_Telemetry.cpp -I$(KAYA_INC_PATH) -I /usr/local/include/ -I /usr/lib/antaris/include -I /usr/lib/antaris/gen $(LDFLAGS) -lKYFGLib $(KYFLAGS) $(LDFLAGS) -lKYFGLib $(KYFLAGS) 

# #########################################
# # Compile object files for Packetization and Frame Formation.
# #########################################
# XDLX_Pack.o: XDLX_Pack.h XDLX_Pack.cpp
# 	c++ -c XDLX_Pack.cpp

# #########################################
# # Final payload binary
# #########################################

# payload_app: payload_app.cc $(KAYA_OBJS)
# 	g++ -o $@ $^ $(INCLUDES) -I./KAYA_Instruments/include \
# 		-L./KAYA_Instruments/lib -Wl,-rpath,/workspace/KAYA_Instruments/lib \
# 		-lKYFGLib -lssh -lssh2 $(LIBS)

# #########################################
# # Clean
# #########################################

# clean:
# 	rm -f $(KAYA_OBJS) payload_app




LDFLAGS += -L${KAYA_VISION_POINT_LIB_PATH}
LDFLAGS += -Wl,-rpath,${KAYA_VISION_POINT_LIB_PATH}
LDFLAGS += -Wl,-rpath,'$$ORIGIN'
#KYFLAGS+=-lz #only needed when KYFGLib is built with $KAYA_ATTR_OPENCV_LINK_MODE == "local"

all: payload_app

payload_app: XDLX_CamApp_V6.o XDLX_Kaya_UI.o XDLX_Kaya_Grabber.o XDLX_Camera.o XDLX_Stream.o XDLX_Telemetry.o payload_app.o XDLX_Pack.o
	c++ -o payload_app XDLX_Kaya_UI.o XDLX_Kaya_Grabber.o XDLX_Camera.o XDLX_Stream.o XDLX_Telemetry.o XDLX_CamApp_V6.o payload_app.o XDLX_Pack.o -I$(KAYA_VISION_POINT_INCLUDE_PATH) $(LDFLAGS) -lKYFGLib $(KYFLAGS)

payload_app.o: sharedParams.h antaris_api.h payload_app.cc
	c++ -c payload_app.cc -I$(KAYA_VISION_POINT_INCLUDE_PATH) $(LDFLAGS) -lKYFGLib $(KYFLAGS)
XDLX_CamApp_V6.o : GenericParam.h sharedParams.h XDLX_CamApp_V6.cpp
	c++ -c XDLX_CamApp_V6.cpp -I$(KAYA_VISION_POINT_INCLUDE_PATH) $(LDFLAGS) -lKYFGLib $(KYFLAGS)
XDLX_Kaya_UI.o : GenericParam.h XDLX_Kaya_UI.h XDLX_Kaya_UI.cpp  #GenericParam.h XDLX_Kaya_UI.h
	c++ -c XDLX_Kaya_UI.cpp -I$(KAYA_VISION_POINT_INCLUDE_PATH) $(LDFLAGS) -lKYFGLib $(KYFLAGS)
XDLX_Kaya_Grabber.o : GenericParam.h XDLX_Kaya_Grabber.h XDLX_Kaya_Grabber.cpp
	c++ -c XDLX_Kaya_Grabber.cpp -I$(KAYA_VISION_POINT_INCLUDE_PATH) $(LDFLAGS) -lKYFGLib $(KYFLAGS)
XDLX_Camera.o : GenericParam.h XDLX_Camera.h XDLX_Camera.cpp
	c++ -c XDLX_Camera.cpp -I$(KAYA_VISION_POINT_INCLUDE_PATH) $(LDFLAGS) -lKYFGLib $(KYFLAGS)
XDLX_Stream.o : GenericParam.h XDLX_Stream.h XDLX_Stream.cpp
	c++ -c XDLX_Stream.cpp -I$(KAYA_VISION_POINT_INCLUDE_PATH) $(LDFLAGS) -lKYFGLib $(KYFLAGS)
XDLX_Telemetry.o : GenericParam.h XDLX_Telemetry.h XDLX_Telemetry.cpp
	c++ -c XDLX_Telemetry.cpp -I$(KAYA_VISION_POINT_INCLUDE_PATH) $(LDFLAGS) -lKYFGLib $(KYFLAGS)

XDLX_Pack.o : XDLX_Pack.h XDLX_Pack.cpp
	c++ -c XDLX_Pack.cpp 


clean :
	rm  -r XDLX_Stream.o XDLX_Kaya_UI.o XDLX_Kaya_Grabber.o XDLX_CamApp_V6.o XDLX_Camera.o XDLX_Telemetry.o XDLX_Pack.o payload_app 
    
