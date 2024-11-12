add_library(fgs_5gsm_lib OBJECT PduSessionEstablishRequest.c
                            PduSessionEstablishmentAccept.c)
target_include_directories(fgs_5gsm_lib PUBLIC .)
