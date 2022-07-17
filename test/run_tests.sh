#!/bin/bash
for ((i=1; i <= 20; i++))
do
diff -s test$i test$i.out
done
