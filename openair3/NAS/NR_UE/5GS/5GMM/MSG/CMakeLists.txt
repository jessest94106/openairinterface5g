add_library(fgs_5gmm_lib OBJECT
    fgs_service_request.c
    RegistrationRequest.c
    RegistrationAccept.c
    FGSIdentityResponse.c
    FGSAuthenticationResponse.c
    FGSNASSecurityModeComplete.c
    RegistrationComplete.c
    FGSUplinkNasTransport.c
    FGSDeregistrationRequestUEOriginating.c
)

target_include_directories(fgs_5gmm_lib PUBLIC . ../IES)
