**5GS NAS Overview and OAI Implementation status**

This document provides an overview of the 5G System Non-Access Stratum (5GS NAS) protocol as specified in 3GPP TS 24.501. It highlights key message types involved in Mobility and Session Management and explains how these are implemented within the OAI software stack. The document outlines the structure of the OAI codebase by detailing current support for encoding, decoding, and unit testing of specific NAS messages, and explains how the UE handles USIM simulation and message generation.

[TOC]

# About 5GS NAS

The 5G System Non-Access Stratum (5GS NAS) is defined in 3GPP TS 24.501. It operates within the control plane, facilitating communication between the User Equipment (UE) and the Access and Mobility Management Function (AMF) over the N1 interface.

Key 5GS NAS messages:

* Mobility Management (5GMM): Handles UE registration, deregistration, authentication and tracking area updates. 5GMM also manages transitions between idle and connected states.
* Session Management (5GSM): Establishes, modifies, and releases PDU sessions. Coordinates with the Session Management Function (SMF) to manage user-plane resources.

# OAI Implementation Status

The following tables lists implemented NAS messages and whether there is an encoder or decoder function, and if a corresponding unit test exists.

| Type  | Message                                   | Encoding | Decoding | Unit test |
|-------|-------------------------------------------|----------|----------|------------|
| 5GMM  | Service Request                           | yes      | yes      | yes        |
| 5GMM  | Service Accept                            | yes      | yes      | yes        |
| 5GMM  | Service Reject                            | yes      | yes      | yes        |
| 5GMM  | Authentication Response                   | yes      | no       | no         |
| 5GMM  | Identity Response                         | yes      | no       | no         |
| 5GMM  | Security Mode Complete                    | yes      | no       | no         |
| 5GMM  | Uplink NAS Transport                      | yes      | no       | no         |
| 5GMM  | Registration Request                      | yes      | yes      | no         |
| 5GMM  | Registration Accept                       | yes      | yes      | yes        |
| 5GMM  | Registration Complete                     | yes      | yes      | no         |
| 5GMM  | Deregistration Request (UE originating)   | yes      | no       | no         |
| 5GSM  | PDU Session Establishment Request         | yes      | no       | no         |
| 5GSM  | PDU Session Establishment Accept          | no       | yes      | no         |

## Code Structure

[openair3/NAS/NR_UE/nr_nas_msg.c](../openair3/NAS/NR_UE/nr_nas_msg.c):
* NAS procedures and message handlers/callbacks
* Integration with RRC (ITTI) and SDAP
* Invokes enc/dec libraries
* Handles 5GMM state and mode

[openair3/NAS/NR_UE/5GS/fgs_nas_lib.c](../openair3/NAS/NR_UE/5GS/fgs_nas_lib.c):
[openair3/NAS/NR_UE/5GS/NR_NAS_defs.h](../openair3/NAS/NR_UE/5GS/NR_NAS_defs.h):
* encoding and decoding functions for 5G NAS message headers and payloads
* relies on 5GMM/5GSM messages libs for payload encoding

[openair3/NAS/NR_UE/5GS/fgs_nas_utils.h](../openair3/NAS/NR_UE/5GS/fgs_nas_utils.h):
* NAS helpers, macros

[openair3/NAS/NR_UE/5GS/5GMM](../openair3/NAS/NR_UE/5GS/5GMM):
* encoding/decoding functions and definitions for 5GMM NAS messages payloads

[openair3/NAS/NR_UE/5GS/5GMM/MSG/fgmm_lib.c](../openair3/NAS/NR_UE/5GS/5GMM/MSG/fgmm_lib.c):
[openair3/NAS/NR_UE/5GS/5GMM/MSG/fgmm_lib.h](../openair3/NAS/NR_UE/5GS/5GMM/MSG/fgmm_lib.h):
* encoding/decoding functions and definitions for common 5GMM IEs

[openair3/NAS/NR_UE/5GS/5GSM](../openair3/NAS/NR_UE/5GS/5GSM):
* encoding and decoding functions for 5GSM NAS messages payloads

# USIM simulation
A new USIM simulation, that parameters are in regular OAI config files

## To open the USIM
init_uicc() takes a parameter: the name of the block in the config file
In case we run several UEs in one process, the variable section name allows to make several UEs configurations in one config file.

The NAS implementation uses this possibility.

## Identityrequest + IdentityResponse
When the UE receives the request, it stores the 5GC request parameters in the UE context ( UE->uicc pointer)
It calls "scheduleNAS", that would trigger a answer (seeing general archtecture, it will probly be a itti message to future RRC or NGAP thread).  

When the scheduler wants to encode the answer, it calls identityResponse()
The UE search for a section in the config file called "uicc"
it encodes the NAS answer with the IMSI, as a 4G compatible authentication.
A future release would encode 5G SUPI as native 5G UICC.

## Authenticationrequest + authenticationResponse
When the UE receives the request, it stores the 5GC request parameters in the UE context ( UE->uicc pointer)
It calls "scheduleNAS", that would trigger a answer (seeing general archtecture, it will probly be a itti message to future RRC or NGAP thread).  

When the scheduler wants to encode the answer, it calls authenticationResponse()
The UE search for a section in the config file called "uicc"
It uses the Milenage parameters of this section to encode a milenage response
A future release would encode 5G new authentication cyphering functions.

## SecurityModeCommand + securityModeComplete
When the UE receives the request it will:
Selected NAS security algorithms: store it for future encoding
Replayed UE security capabilities: check if it is equal to UE capabilities it sent to the 5GC
IMEISV request: to implement
ngKSI: Key set Indicator, similator to 4G to select a ciphering keys set

When the scheduler wants to encode the answer, it calls registrationComplete() 

## registrationComplete
To be defined in UE, this NAS message is done after RRC sequence completes

# gNB side
gNB NGAP thread receives the NAS message from PHY layers
In normal mode, it send it to the core network with no decoding.

Here after, the gNB mode "noCore" processing in gNB: NGAP calls the entry function: processNAS() instead of forwarding the packet to the 5GC

## RRCModeComplete + Identityrequest
When the gNB completes RRC attach, it sends a first message to NGAP thread.
Normal processing sends NGAP initial UE message, in noCore mode, it should call: identityRequest() that encode the NAS identity request inside the gNB.  
The gNB NGAP thread then should call the piece of code that forward the message to the UE as when it receives a NAS message from 5GC.  
