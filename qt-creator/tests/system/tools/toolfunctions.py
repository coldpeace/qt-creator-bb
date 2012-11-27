#!/usr/bin/env python

import os
import sys

def checkDirectory(directory):
    if not os.path.exists(directory):
        print "Given path '%s' does not exist" % directory
        sys.exit(1)
    objMap = os.path.join(directory, "objects.map")
    if not os.path.exists(objMap):
        print "Given path '%s' does not contain an objects.map file" % directory
        sys.exit(1)
    return objMap

def getFileContent(filePath):
    if os.path.isfile(filePath):
        f = open(filePath, "r")
        data = f.read()
        f.close()
        return data
    return ""
