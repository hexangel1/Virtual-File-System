#!/bin/bash
for ((i=1; i <= 10; i++))
do
diff -s test$i test$i.out
done
