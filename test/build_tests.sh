#!/bin/bash
for ((i=1; i <= 10; i++))
do
dd if=/dev/urandom of=test$i count=16000 status=none
done
