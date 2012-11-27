import qbs.base 1.0

import "../QtcPlugin.qbs" as QtcPlugin
import "../../../qbs/defaults.js" as Defaults

QtcPlugin {
    name: "CppTools"

    Depends { name: "Qt.widgets" }
    Depends { name: "Core" }
    Depends { name: "Find" }
    Depends { name: "TextEditor" }
    Depends { name: "ProjectExplorer" }
    Depends { name: "Locator" }
    Depends { name: "CPlusPlus" }
    Depends { name: "LanguageUtils" }

    Depends { name: "cpp" }
    cpp.defines: base.concat(["QT_NO_CAST_FROM_ASCII"])

    files: [
        "ModelManagerInterface.cpp",
        "ModelManagerInterface.h",
        "TypeHierarchyBuilder.cpp",
        "TypeHierarchyBuilder.h",
        "abstracteditorsupport.cpp",
        "abstracteditorsupport.h",
        "commentssettings.cpp",
        "commentssettings.h",
        "completionsettingspage.cpp",
        "completionsettingspage.h",
        "completionsettingspage.ui",
        "cppchecksymbols.cpp",
        "cppchecksymbols.h",
        "cppclassesfilter.cpp",
        "cppclassesfilter.h",
        "cppcodeformatter.cpp",
        "cppcodeformatter.h",
        "cppcodestylepreferences.cpp",
        "cppcodestylepreferences.h",
        "cppcodestylepreferencesfactory.cpp",
        "cppcodestylepreferencesfactory.h",
        "cppcodestylesettings.cpp",
        "cppcodestylesettings.h",
        "cppcodestylesettingspage.cpp",
        "cppcodestylesettingspage.h",
        "cppcodestylesettingspage.ui",
        "cppcompletionassist.cpp",
        "cppcompletionassist.h",
        "cppcompletionassistprovider.cpp",
        "cppcompletionassistprovider.h",
        "cppcompletionsupport.cpp",
        "cppcompletionsupport.h",
        "cppcurrentdocumentfilter.cpp",
        "cppcurrentdocumentfilter.h",
        "cppdoxygen.cpp",
        "cppdoxygen.h",
        "cppfilesettingspage.cpp",
        "cppfilesettingspage.h",
        "cppfilesettingspage.ui",
        "cppfindreferences.cpp",
        "cppfindreferences.h",
        "cppfunctionsfilter.cpp",
        "cppfunctionsfilter.h",
        "cpphighlightingsupport.cpp",
        "cpphighlightingsupport.h",
        "cpphighlightingsupportinternal.cpp",
        "cpphighlightingsupportinternal.h",
        "cppindexingsupport.cpp",
        "cppindexingsupport.h",
        "cpplocalsymbols.cpp",
        "cpplocalsymbols.h",
        "cpplocatorfilter.cpp",
        "cpplocatorfilter.h",
        "cppmodelmanager.cpp",
        "cppmodelmanager.h",
        "cppqtstyleindenter.cpp",
        "cppqtstyleindenter.h",
        "cpprefactoringchanges.cpp",
        "cpprefactoringchanges.h",
        "cppsemanticinfo.cpp",
        "cppsemanticinfo.h",
        "cpptools_global.h",
        "cpptoolsconstants.h",
        "cpptoolseditorsupport.cpp",
        "cpptoolseditorsupport.h",
        "cpptoolsplugin.cpp",
        "cpptoolsplugin.h",
        "cpptoolsreuse.cpp",
        "cpptoolsreuse.h",
        "cpptoolssettings.cpp",
        "cpptoolssettings.h",
        "doxygengenerator.cpp",
        "doxygengenerator.h",
        "insertionpointlocator.cpp",
        "insertionpointlocator.h",
        "searchsymbols.cpp",
        "searchsymbols.h",
        "symbolfinder.cpp",
        "symbolfinder.h",
        "symbolsfindfilter.cpp",
        "symbolsfindfilter.h",
        "uicodecompletionsupport.cpp",
        "uicodecompletionsupport.h",
    ]

    Group {
        condition: Defaults.testsEnabled(qbs)
        files: ["cppcodegen_test.cpp", "cppcompletion_test.cpp"]
    }

    ProductModule {
        Depends { name: "CPlusPlus" }
    }
}
