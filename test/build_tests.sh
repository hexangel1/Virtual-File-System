#!/bin/bash
for ((i=1; i <= 20; i++))
do
dd if=/dev/urandom of=test$i count=10000 status=none
done
