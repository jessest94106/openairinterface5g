#!/bin/bash

die() { echo -e "$@"; exit 1; }

[[ -z "$1" ]] && die "Usage: $0 <distance_in_meters>"

IP=192.168.71.150
PORT=8091
distance=$1

set_and_verify_distance() {
  local distance=$1
  echo "Testing PRS ToA estimation for distance: $distance m"

  # it sems that grep returns immediately with this syntax, but not echo | ncat | grep
  # so prefer this to receive new distance immediately. We use --idle to keep
  # ncat open for some additional time
  local setdist_resp="$(grep --max-count 1 new_offset <(echo rfsimu setdistance rfsimu_channel_enB0 $distance | ncat --idle 1 ${IP} ${PORT}))"
  echo "> response: ${setdist_resp}"
  local gettoa_resp="$(echo ciUE get_max_dl_toa | ncat ${IP} ${PORT} | grep "UE max PRS DL ToA")"
  echo "> response: ${gettoa_resp}"

  [[ "$setdist_resp" =~ new_offset\ ([0-9]+) ]] || die "Set ToA extraction failed for distance: $distance m\nLOG: $setdist_resp"
  local set_toa="${BASH_REMATCH[1]}"
  
  [[ "$gettoa_resp" =~ UE\ max\ PRS\ DL\ ToA\ ([0-9]+) ]] || die "Estimated ToA extraction failed for distance: $distance m\nLOG: $gettoa_resp"
  local est_toa="${BASH_REMATCH[1]}"
  
  [[ $set_toa == $est_toa ]] || die "PRS FAILURE for distance: $distance m\nActual ToA = $set_toa, Estimated ToA = $est_toa"
  
  echo "PRS SUCCESS for distance: $distance m"
}

# Set distance to 0 initially and then set the actual distance
sleep 4
set_and_verify_distance 0

sleep 4
set_and_verify_distance $distance
