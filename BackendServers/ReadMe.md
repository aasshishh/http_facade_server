# BUILD
Commands
* `cd backendify`
* `g++ -std=c++17 .\BackendServers\BackendApi.cpp -o .\build\BackendApi.exe -lws2_32 -pthread`

# RUN
* `.\build\BackendApi.exe`

# TEST
* `curl "http://localhost:9001/companies/<company_id>"` 

# SUPPORTED INPUTS
Following company_id(s) for supported for testing.
* *Format V1 :*
    12345,
    123456,
    1234567
* *Format V2 :*
    2345,
    23456,
    234567