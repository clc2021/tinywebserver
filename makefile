CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2

endif

# CXXFLAGS += -std=c++11

myserver: main.cpp  ./timer/wheel_timer.cpp ./http/http_conn.cpp ./log/log.cpp ./CGImysql/sql_connection_pool.cpp webserver.cpp config.cpp
	$(CXX) -g -o myserver  $^ $(CXXFLAGS) -lpthread -lmysqlclient -lrt

# myserver: main.cpp  ./timer/wheel_timer.cpp ./http/http_conn.cpp ./log/log.cpp ./CGImysql/sql_connection_pool.cpp  fiber_webserver.cpp config.cpp ./threadpool/iomanager.cc ./threadpool/fiber.cc ./threadpool/thread.cc ./threadpool/scheduler.cc ./lock/mutex.cc ./timer/timer.cc
# 	$(CXX) -g -o myserver  $^ $(CXXFLAGS) -lpthread -lmysqlclient -lrt

clean:
	rm  -r myserver
