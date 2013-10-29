#!/bin/bash
for i in *.log; do
	cat $i | tail -n 1
done
