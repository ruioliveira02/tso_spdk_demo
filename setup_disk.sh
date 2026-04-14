#!/bin/bash

touch nvme.img
dd if=/dev/random of=nvme.img bs=4k count=25600

