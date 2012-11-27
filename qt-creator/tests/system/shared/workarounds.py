import urllib2
import re

JIRA_URL='https://bugreports.qt-project.org/browse'

class JIRA:
    __instance__ = None

    # Helper class
    class Bug:
        CREATOR = 'QTCREATORBUG'
        SIMULATOR = 'QTSIM'
        SDK = 'QTSDK'
        QT = 'QTBUG'
        QT_QUICKCOMPONENTS = 'QTCOMPONENTS'

    # constructor of JIRA
    def __init__(self, number, bugType=Bug.CREATOR):
        if JIRA.__instance__ == None:
            JIRA.__instance__ = JIRA.__impl(number, bugType)
            JIRA.__dict__['_JIRA__instance__'] = JIRA.__instance__
        else:
            JIRA.__instance__._bugType = bugType
            JIRA.__instance__._number = number
            JIRA.__instance__.__fetchStatusAndResolutionFromJira__()

    # overriden to make it possible to use JIRA just like the
    # underlying implementation (__impl)
    def __getattr__(self, attr):
        return getattr(self.__instance__, attr)

    # overriden to make it possible to use JIRA just like the
    # underlying implementation (__impl)
    def __setattr__(self, attr, value):
        return setattr(self.__instance__, attr, value)

    # function to get an instance of the singleton
    @staticmethod
    def getInstance():
        if '_JIRA__instance__' in JIRA.__dict__:
            return JIRA.__instance__
        else:
            return JIRA.__impl(0, Bug.CREATOR)

    # function to check if the given bug is open or not
    @staticmethod
    def isBugStillOpen(number, bugType=Bug.CREATOR):
        tmpJIRA = JIRA(number, bugType)
        return tmpJIRA.isOpen()

    # function similar to performWorkaroundForBug - but it will execute the
    # workaround (function) only if the bug is still open
    # returns True if the workaround function has been executed, False otherwise
    @staticmethod
    def performWorkaroundIfStillOpen(number, bugType=Bug.CREATOR, *args):
        if JIRA.isBugStillOpen(number, bugType):
            return JIRA.performWorkaroundForBug(number, bugType, *args)
        else:
            test.warning("Bug is closed... skipping workaround!",
                         "You should remove potential code inside performWorkaroundForBug()")
            return False

    # function that performs the workaround (function) for the given bug
    # if the function needs additional arguments pass them as 3rd parameter
    @staticmethod
    def performWorkaroundForBug(number, bugType=Bug.CREATOR, *args):
        functionToCall = JIRA.getInstance().__bugs__.get("%s-%d" % (bugType, number), None)
        if functionToCall:
            test.warning("Using workaround for %s-%d" % (bugType, number))
            functionToCall(*args)
            return True
        else:
            JIRA.getInstance()._exitFatal_(bugType, number)
            return False

    # implementation of JIRA singleton
    class __impl:
        # constructor of __impl
        def __init__(self, number, bugType):
            self._number = number
            self._bugType = bugType
            self._localOnly = os.getenv("SYSTEST_JIRA_NO_LOOKUP")=="1"
            self.__initBugDict__()
            self._fetchResults_ = {}
            self.__fetchStatusAndResolutionFromJira__()

        # function to retrieve the status of the current bug
        def getStatus(self):
            return self._status

        # function to retrieve the resolution of the current bug
        def getResolution(self):
            return self._resolution

        # this function checks the resolution of the given bug
        # and returns True if the bug can still be assumed as 'Open' and False otherwise
        def isOpen(self):
            # handle special cases
            if self._resolution == None:
                return True
            if self._resolution in ('Duplicate', 'Moved', 'Incomplete', 'Cannot Reproduce', 'Invalid'):
                test.warning("Resolution of bug is '%s' - assuming 'Open' for now." % self._resolution,
                             "Please check the bugreport manually and update this test.")
                return True
            return self._resolution != 'Done'

        # this function tries to fetch the status and resolution from JIRA for the given bug
        # if this isn't possible or the lookup is disabled it does only check the internal
        # dict whether a function for the given bug is deposited or not
        def __fetchStatusAndResolutionFromJira__(self):
            global JIRA_URL
            bug = "%s-%d" % (self._bugType, self._number)
            if bug in self._fetchResults_:
                result = self._fetchResults_[bug]
                self._resolution = result[0]
                self._status = result[1]
                return
            data = None
            proxy = os.getenv("SYSTEST_PROXY", None)
            if not self._localOnly:
                try:
                    if proxy:
                        proxy = urllib2.ProxyHandler({'https': proxy})
                        opener = urllib2.build_opener(proxy)
                        urllib2.install_opener(opener)
                    bugReport = urllib2.urlopen('%s/%s' % (JIRA_URL, bug))
                    data = bugReport.read()
                except:
                    data = self.__tryExternalTools__(proxy)
                    if data == None:
                        test.warning("Sorry, ssl module missing - cannot fetch data via HTTPS",
                                     "Try to install the ssl module by yourself, or set the python "
                                     "path inside SQUISHDIR/etc/paths.ini to use a python version with "
                                     "ssl support OR install wget or curl to get rid of this warning!")
                        self._localOnly = True
            if data == None:
                if bug in self.__bugs__:
                    test.warning("Using internal dict - bug status could have changed already",
                                 "Please check manually!")
                    self._status = None
                    self._resolution = None
                else:
                    test.fatal("No workaround function deposited for %s" % bug)
                    self._resolution = 'Done'
            else:
                data = data.replace("\r", "").replace("\n", "")
                resPattern = re.compile('<span\s+id="resolution-val".*?>(?P<resolution>.*?)</span>')
                statPattern = re.compile('<span\s+id="status-val".*?>(.*?<img.*?>)?(?P<status>.*?)</span>')
                status = statPattern.search(data)
                resolution = resPattern.search(data)
                if status:
                    self._status = status.group("status").strip()
                else:
                    test.fatal("FATAL: Cannot get status of bugreport %s" % bug,
                               "Looks like JIRA has changed.... Please verify!")
                    self._status = None
                if resolution:
                    self._resolution = resolution.group("resolution").strip()
                else:
                    test.fatal("FATAL: Cannot get resolution of bugreport %s" % bug,
                               "Looks like JIRA has changed.... Please verify!")
                    self._resolution = None
            if None in (self._status, self._resolution):
                self.__cropAndLog__(data)
            self._fetchResults_.update({bug:[self._resolution, self._status]})

        # simple helper function - used as fallback if python has no ssl support
        # tries to find curl or wget in PATH and fetches data with it instead of
        # using urllib2
        def __tryExternalTools__(self, proxy=None):
            global JIRA_URL
            if proxy:
                cmdAndArgs = { 'curl':'-k --proxy %s' % proxy,
                               'wget':'-qO-'}
            else:
                cmdAndArgs = { 'curl':'-k', 'wget':'-qO-' }
            for call in cmdAndArgs:
                prog = which(call)
                if prog:
                    if call == 'wget' and proxy and os.getenv("https_proxy", None) == None:
                        test.warning("Missing environment variable https_proxy for using wget with proxy!")
                    return getOutputFromCmdline('"%s" %s %s/%s-%d' % (prog, cmdAndArgs[call], JIRA_URL, self._bugType, self._number))
            return None

        # this function crops multiple whitespaces from fetched and searches for expected
        # ids without using regex
        def __cropAndLog__(self, fetched):
            if fetched == None:
                test.log("None passed to __cropAndLog__()")
                return
            fetched = " ".join(fetched.split())
            resoInd = fetched.find('resolution-val')
            statInd = fetched.find('status-val')
            if resoInd == statInd == -1:
                test.log("Neither resolution nor status found inside fetched data.",
                         "%s[...]" % fetched[:200])
            else:
                if resoInd == -1:
                    test.log("Fetched and cropped data: [...]%s[...]" % fetched[statInd-20:statInd+800])
                elif statInd == -1:
                    test.log("Fetched and cropped data: [...]%s[...]" % fetched[resoInd-720:resoInd+100])
                else:
                    test.log("Fetched and cropped data (status): [...]%s[...]" % fetched[statInd-20:statInd+300],
                             "Fetched and cropped data (resolution): [...]%s[...]" % fetched[resoInd-20:resoInd+100])

        # this function initializes the bug dict for localOnly usage and
        # for later lookup which function to call for which bug
        # ALWAYS update this dict when adding a new function for a workaround!
        def __initBugDict__(self):
            self.__bugs__= {
                            'QTCREATORBUG-6853':self._workaroundCreator6853_,
                            'QTCREATORBUG-6918':self._workaroundCreator_MacEditorFocus_
                            }
        # helper function - will be called if no workaround for the requested bug is deposited
        def _exitFatal_(self, bugType, number):
            test.fatal("No workaround found for bug %s-%d" % (bugType, number))

############### functions that hold workarounds #################################

        def _workaroundCreator6853_(self, *args):
            if "Release" in args[0] and platform.system() == "Linux":
                snooze(2)

        def _workaroundCreator_MacEditorFocus_(self, *args):
            editor = args[0]
            nativeMouseClick(editor.mapToGlobal(QPoint(50, 50)).x, editor.mapToGlobal(QPoint(50, 50)).y, Qt.LeftButton)
