import qbs.base 1.0

import "../QtcPlugin.qbs" as QtcPlugin

QtcPlugin {
    name: "QmlDesigner"

    condition: qtcore.versionMajor == 4
    Depends { id: qtcore; name: "Qt.core" }
    Depends { name: "Qt"; submodules: ["widgets", "declarative"] }
    Depends { name: "Core" }
    Depends { name: "QmlJS" }
    Depends { name: "QmlEditorWidgets" }
    Depends { name: "TextEditor" }
    Depends { name: "QmlJSEditor" }
    Depends { name: "Qt4ProjectManager" }
    Depends { name: "QmlProjectManager" }
    Depends { name: "ProjectExplorer" }
    Depends { name: "LanguageUtils" }
    Depends { name: "QtSupport" }

    Depends { name: "cpp" }
    cpp.defines: base.concat(["QWEAKPOINTER_ENABLE_ARROW"])
    cpp.includePaths: base.concat([
        "designercore",
        "designercore/include",
        "../../../share/qtcreator/qml/qmlpuppet/interfaces",
        "../../../share/qtcreator/qml/qmlpuppet/container",
        "../../../share/qtcreator/qml/qmlpuppet/commands",
        "components/componentcore",
        "components/integration",
        "components/propertyeditor",
        "components/formeditor",
        "components/itemlibrary",
        "components/navigator",
        "components/pluginmanager",
        "components/stateseditor"
    ])

    Group {
        prefix: "designercore/filemanager/"
        files: [
            "addarraymembervisitor.cpp",
            "addarraymembervisitor.h",
            "addobjectvisitor.cpp",
            "addobjectvisitor.h",
            "addpropertyvisitor.cpp",
            "addpropertyvisitor.h",
            "astobjecttextextractor.cpp",
            "astobjecttextextractor.h",
            "changeimportsvisitor.cpp",
            "changeimportsvisitor.h",
            "changeobjecttypevisitor.cpp",
            "changeobjecttypevisitor.h",
            "changepropertyvisitor.cpp",
            "changepropertyvisitor.h",
            "firstdefinitionfinder.cpp",
            "firstdefinitionfinder.h",
            "moveobjectbeforeobjectvisitor.cpp",
            "moveobjectbeforeobjectvisitor.h",
            "moveobjectvisitor.cpp",
            "moveobjectvisitor.h",
            "objectlengthcalculator.cpp",
            "objectlengthcalculator.h",
            "qmlrefactoring.cpp",
            "qmlrefactoring.h",
            "qmlrewriter.cpp",
            "qmlrewriter.h",
            "removepropertyvisitor.cpp",
            "removepropertyvisitor.h",
            "removeuiobjectmembervisitor.cpp",
            "removeuiobjectmembervisitor.h",
        ]
    }

    Group {
        prefix: "../../../share/qtcreator/qml/qmlpuppet/"
        files: [
            "commands/changeauxiliarycommand.cpp",
            "commands/changeauxiliarycommand.h",
            "commands/changebindingscommand.cpp",
            "commands/changebindingscommand.h",
            "commands/changefileurlcommand.cpp",
            "commands/changefileurlcommand.h",
            "commands/changeidscommand.cpp",
            "commands/changeidscommand.h",
            "commands/changenodesourcecommand.cpp",
            "commands/changenodesourcecommand.h",
            "commands/changestatecommand.cpp",
            "commands/changestatecommand.h",
            "commands/changevaluescommand.cpp",
            "commands/changevaluescommand.h",
            "commands/childrenchangedcommand.cpp",
            "commands/childrenchangedcommand.h",
            "commands/clearscenecommand.cpp",
            "commands/clearscenecommand.h",
            "commands/completecomponentcommand.cpp",
            "commands/completecomponentcommand.h",
            "commands/componentcompletedcommand.cpp",
            "commands/componentcompletedcommand.h",
            "commands/createinstancescommand.cpp",
            "commands/createinstancescommand.h",
            "commands/createscenecommand.cpp",
            "commands/createscenecommand.h",
            "commands/informationchangedcommand.cpp",
            "commands/informationchangedcommand.h",
            "commands/pixmapchangedcommand.cpp",
            "commands/pixmapchangedcommand.h",
            "commands/removeinstancescommand.cpp",
            "commands/removeinstancescommand.h",
            "commands/removepropertiescommand.cpp",
            "commands/removepropertiescommand.h",
            "commands/reparentinstancescommand.cpp",
            "commands/reparentinstancescommand.h",
            "commands/statepreviewimagechangedcommand.cpp",
            "commands/statepreviewimagechangedcommand.h",
            "commands/synchronizecommand.cpp",
            "commands/synchronizecommand.h",
            "commands/tokencommand.cpp",
            "commands/tokencommand.h",
            "commands/valueschangedcommand.cpp",
            "commands/valueschangedcommand.h",
            "container/addimportcontainer.cpp",
            "container/addimportcontainer.h",
            "container/idcontainer.cpp",
            "container/idcontainer.h",
            "container/imagecontainer.cpp",
            "container/imagecontainer.h",
            "container/informationcontainer.cpp",
            "container/informationcontainer.h",
            "container/instancecontainer.cpp",
            "container/instancecontainer.h",
            "container/propertyabstractcontainer.cpp",
            "container/propertyabstractcontainer.h",
            "container/propertybindingcontainer.cpp",
            "container/propertybindingcontainer.h",
            "container/propertyvaluecontainer.cpp",
            "container/propertyvaluecontainer.h",
            "container/reparentcontainer.cpp",
            "container/reparentcontainer.h",
            "interfaces/commondefines.h",
            "interfaces/nodeinstanceclientinterface.h",
            "interfaces/nodeinstanceserverinterface.cpp",
            "interfaces/nodeinstanceserverinterface.h",
        ]
    }

    Group {
        prefix: "designercore/"
        files: [
            "rewritertransaction.cpp",
            "rewritertransaction.h",
            "exceptions/exception.cpp",
            "exceptions/invalidargumentexception.cpp",
            "exceptions/invalididexception.cpp",
            "exceptions/invalidmetainfoexception.cpp",
            "exceptions/invalidmodelnodeexception.cpp",
            "exceptions/invalidmodelstateexception.cpp",
            "exceptions/invalidpropertyexception.cpp",
            "exceptions/invalidqmlsourceexception.cpp",
            "exceptions/invalidreparentingexception.cpp",
            "exceptions/invalidslideindexexception.cpp",
            "exceptions/notimplementedexception.cpp",
            "exceptions/removebasestateexception.cpp",
            "exceptions/rewritingexception.cpp",
            "include/abstractproperty.h",
            "include/abstractview.h",
            "include/basetexteditmodifier.h",
            "include/basetexteditmodifier.h",
            "include/bindingproperty.h",
            "include/componenttextmodifier.h",
            "include/corelib_global.h",
            "include/customnotifications.h",
            "include/exception.h",
            "include/forwardview.h",
            "include/import.h",
            "include/invalidargumentexception.h",
            "include/invalididexception.h",
            "include/invalidmetainfoexception.h",
            "include/invalidmodelstateexception.h",
            "include/invalidpropertyexception.h",
            "include/invalidqmlsourceexception.h",
            "include/invalidreparentingexception.h",
            "include/invalidslideindexexception.h",
            "include/itemlibraryinfo.h",
            "include/mathutils.h",
            "include/metainfo.h",
            "include/metainfoparser.h",
            "include/model.h",
            "include/modelmerger.h",
            "include/modelnode.h",
            "include/modelnodepositionstorage.h",
            "include/nodeabstractproperty.h",
            "include/nodeinstance.h",
            "include/nodeinstanceview.h",
            "include/nodelistproperty.h",
            "include/nodemetainfo.h",
            "include/nodeproperty.h",
            "include/notimplementedexception.h",
            "include/plaintexteditmodifier.h",
            "include/propertycontainer.h",
            "include/propertynode.h",
            "include/propertyparser.h",
            "include/qmlanchors.h",
            "include/qmlchangeset.h",
            "include/qmlitemnode.h",
            "include/qmlmodelnodefacade.h",
            "include/qmlmodelview.h",
            "include/qmlobjectnode.h",
            "include/qmlstate.h",
            "include/removebasestateexception.h",
            "include/rewriterview.h",
            "include/rewritingexception.h",
            "include/subcomponentmanager.h",
            "include/textmodifier.h",
            "include/variantproperty.h",
            "instances/nodeinstance.cpp",
            "instances/nodeinstanceserverproxy.cpp",
            "instances/nodeinstanceserverproxy.h",
            "instances/nodeinstanceview.cpp",
            "metainfo/itemlibraryinfo.cpp",
            "metainfo/metainfo.cpp",
            "metainfo/metainfoparser.cpp",
            "metainfo/nodemetainfo.cpp",
            "metainfo/subcomponentmanager.cpp",
            "model/abstractproperty.cpp",
            "model/abstractview.cpp",
            "model/basetexteditmodifier.cpp",
            "model/bindingproperty.cpp",
            "model/componenttextmodifier.cpp",
            "model/import.cpp",
            "model/internalbindingproperty.cpp",
            "model/internalbindingproperty.h",
            "model/internalnode.cpp",
            "model/internalnode_p.h",
            "model/internalnodeabstractproperty.cpp",
            "model/internalnodeabstractproperty.h",
            "model/internalnodelistproperty.cpp",
            "model/internalnodelistproperty.h",
            "model/internalnodeproperty.cpp",
            "model/internalnodeproperty.h",
            "model/internalproperty.cpp",
            "model/internalproperty.h",
            "model/internalvariantproperty.cpp",
            "model/internalvariantproperty.h",
            "model/model.cpp",
            "model/model_p.h",
            "model/modelmerger.cpp",
            "model/modelnode.cpp",
            "model/modelnodepositionrecalculator.cpp",
            "model/modelnodepositionrecalculator.h",
            "model/modelnodepositionstorage.cpp",
            "model/modeltotextmerger.cpp",
            "model/modeltotextmerger.h",
            "model/nodeabstractproperty.cpp",
            "model/nodelistproperty.cpp",
            "model/nodeproperty.cpp",
            "model/painteventfilter.cpp",
            "model/painteventfilter_p.h",
            "model/plaintexteditmodifier.cpp",
            "model/propertycontainer.cpp",
            "model/propertynode.cpp",
            "model/propertyparser.cpp",
            "model/qmlanchors.cpp",
            "model/qmlchangeset.cpp",
            "model/qmlitemnode.cpp",
            "model/qmlmodelnodefacade.cpp",
            "model/qmlmodelview.cpp",
            "model/qmlobjectnode.cpp",
            "model/qmlstate.cpp",
            "model/qmltextgenerator.cpp",
            "model/qmltextgenerator.h",
            "model/rewriteaction.cpp",
            "model/rewriteaction.h",
            "model/rewriteactioncompressor.cpp",
            "model/rewriteactioncompressor.h",
            "model/rewriterview.cpp",
            "model/textmodifier.cpp",
            "model/texttomodelmerger.cpp",
            "model/texttomodelmerger.h",
            "model/variantproperty.cpp",
            "model/viewlogger.cpp",
            "model/viewlogger.h",
            "pluginmanager/widgetpluginmanager.cpp",
            "pluginmanager/widgetpluginmanager.h",
            "pluginmanager/widgetpluginpath.cpp",
            "pluginmanager/widgetpluginpath.h",
        ]
    }

    Group {
        prefix: "components/"
        files: [
            "componentcore/modelnodecontextmenu.cpp",
            "componentcore/modelnodecontextmenu.h",
            "formeditor/abstractformeditortool.cpp",
            "formeditor/abstractformeditortool.h",
            "formeditor/controlelement.cpp",
            "formeditor/controlelement.h",
            "formeditor/dragtool.cpp",
            "formeditor/dragtool.h",
            "formeditor/formeditor.qrc",
            "formeditor/formeditorgraphicsview.cpp",
            "formeditor/formeditorgraphicsview.h",
            "formeditor/formeditoritem.cpp",
            "formeditor/formeditoritem.h",
            "formeditor/formeditorscene.cpp",
            "formeditor/formeditorscene.h",
            "formeditor/formeditorview.cpp",
            "formeditor/formeditorview.h",
            "formeditor/formeditorwidget.cpp",
            "formeditor/formeditorwidget.h",
            "formeditor/itemutilfunctions.cpp",
            "formeditor/itemutilfunctions.h",
            "formeditor/layeritem.cpp",
            "formeditor/layeritem.h",
            "formeditor/lineeditaction.cpp",
            "formeditor/lineeditaction.h",
            "formeditor/movemanipulator.cpp",
            "formeditor/movemanipulator.h",
            "formeditor/movetool.cpp",
            "formeditor/movetool.h",
            "formeditor/numberseriesaction.cpp",
            "formeditor/numberseriesaction.h",
            "formeditor/onedimensionalcluster.cpp",
            "formeditor/onedimensionalcluster.h",
            "formeditor/resizecontroller.cpp",
            "formeditor/resizecontroller.h",
            "formeditor/resizehandleitem.cpp",
            "formeditor/resizehandleitem.h",
            "formeditor/resizeindicator.cpp",
            "formeditor/resizeindicator.h",
            "formeditor/resizemanipulator.cpp",
            "formeditor/resizemanipulator.h",
            "formeditor/resizetool.cpp",
            "formeditor/resizetool.h",
            "formeditor/rubberbandselectionmanipulator.cpp",
            "formeditor/rubberbandselectionmanipulator.h",
            "formeditor/scaleitem.cpp",
            "formeditor/scaleitem.h",
            "formeditor/scalemanipulator.cpp",
            "formeditor/scalemanipulator.h",
            "formeditor/selectionindicator.cpp",
            "formeditor/selectionindicator.h",
            "formeditor/selectionrectangle.cpp",
            "formeditor/selectionrectangle.h",
            "formeditor/selectiontool.cpp",
            "formeditor/selectiontool.h",
            "formeditor/singleselectionmanipulator.cpp",
            "formeditor/singleselectionmanipulator.h",
            "formeditor/snapper.cpp",
            "formeditor/snapper.h",
            "formeditor/snappinglinecreator.cpp",
            "formeditor/snappinglinecreator.h",
            "formeditor/toolbox.cpp",
            "formeditor/toolbox.h",
            "formeditor/zoomaction.cpp",
            "formeditor/zoomaction.h",
            "integration/componentaction.cpp",
            "integration/componentaction.h",
            "integration/componentview.cpp",
            "integration/componentview.h",
            "integration/designdocumentcontroller.cpp",
            "integration/designdocumentcontroller.h",
            "integration/designdocumentcontrollerview.cpp",
            "integration/designdocumentcontrollerview.h",
            "integration/stackedutilitypanelcontroller.cpp",
            "integration/stackedutilitypanelcontroller.h",
            "integration/utilitypanelcontroller.cpp",
            "integration/utilitypanelcontroller.h",
            "integration/xuifiledialog.cpp",
            "integration/xuifiledialog.h",
            "itemlibrary/customdraganddrop.cpp",
            "itemlibrary/customdraganddrop.h",
            "itemlibrary/itemlibrary.qrc",
            "itemlibrary/itemlibrarycomponents.cpp",
            "itemlibrary/itemlibrarycomponents.h",
            "itemlibrary/itemlibraryimageprovider.cpp",
            "itemlibrary/itemlibraryimageprovider.h",
            "itemlibrary/itemlibrarymodel.cpp",
            "itemlibrary/itemlibrarymodel.h",
            "itemlibrary/itemlibraryview.cpp",
            "itemlibrary/itemlibraryview.h",
            "itemlibrary/itemlibrarywidget.cpp",
            "itemlibrary/itemlibrarywidget.h",
            "itemlibrary/qml/ItemView.qml",
            "itemlibrary/qml/ItemsView.qml",
            "itemlibrary/qml/ItemsViewStyle.qml",
            "itemlibrary/qml/Scrollbar.qml",
            "itemlibrary/qml/SectionView.qml",
            "itemlibrary/qml/Selector.qml",
            "navigator/navigator.qrc",
            "navigator/navigatortreemodel.cpp",
            "navigator/navigatortreemodel.h",
            "navigator/navigatortreeview.cpp",
            "navigator/navigatortreeview.h",
            "navigator/navigatorview.cpp",
            "navigator/navigatorview.h",
            "navigator/navigatorwidget.cpp",
            "navigator/navigatorwidget.h",
            "pluginmanager/iplugin.cpp",
            "pluginmanager/iplugin.h",
            "pluginmanager/pluginmanager.cpp",
            "pluginmanager/pluginmanager.h",
            "pluginmanager/pluginpath.cpp",
            "pluginmanager/pluginpath.h",
            "propertyeditor/basiclayouts.cpp",
            "propertyeditor/basiclayouts.h",
            "propertyeditor/basicwidgets.cpp",
            "propertyeditor/basicwidgets.h",
            "propertyeditor/behaviordialog.cpp",
            "propertyeditor/behaviordialog.h",
            "propertyeditor/behaviordialog.ui",
            "propertyeditor/declarativewidgetview.cpp",
            "propertyeditor/declarativewidgetview.h",
            "propertyeditor/designerpropertymap.h",
            "propertyeditor/filewidget.cpp",
            "propertyeditor/filewidget.h",
            "propertyeditor/fontwidget.cpp",
            "propertyeditor/fontwidget.h",
            "propertyeditor/gradientlineqmladaptor.cpp",
            "propertyeditor/gradientlineqmladaptor.h",
            "propertyeditor/layoutwidget.cpp",
            "propertyeditor/layoutwidget.h",
            "propertyeditor/originwidget.cpp",
            "propertyeditor/originwidget.h",
            "propertyeditor/propertyeditor.cpp",
            "propertyeditor/propertyeditor.h",
            "propertyeditor/propertyeditor.qrc",
            "propertyeditor/propertyeditorcontextobject.cpp",
            "propertyeditor/propertyeditorcontextobject.h",
            "propertyeditor/propertyeditortransaction.cpp",
            "propertyeditor/propertyeditortransaction.h",
            "propertyeditor/propertyeditorvalue.cpp",
            "propertyeditor/propertyeditorvalue.h",
            "propertyeditor/qlayoutobject.cpp",
            "propertyeditor/qlayoutobject.h",
            "propertyeditor/qmlanchorbindingproxy.cpp",
            "propertyeditor/qmlanchorbindingproxy.h",
            "propertyeditor/qproxylayoutitem.cpp",
            "propertyeditor/qproxylayoutitem.h",
            "propertyeditor/resetwidget.cpp",
            "propertyeditor/resetwidget.h",
            "propertyeditor/siblingcombobox.cpp",
            "propertyeditor/siblingcombobox.h",
            "resources/resources.qrc",
            "stateseditor/HorizontalScrollBar.qml",
            "stateseditor/stateseditor.qrc",
            "stateseditor/stateseditorimageprovider.cpp",
            "stateseditor/stateseditorimageprovider.cpp",
            "stateseditor/stateseditormodel.cpp",
            "stateseditor/stateseditormodel.h",
            "stateseditor/stateseditorview.cpp",
            "stateseditor/stateseditorview.h",
            "stateseditor/stateseditorwidget.cpp",
            "stateseditor/stateseditorwidget.h",
            "stateseditor/stateslist.qml",
        ]
    }

    files: [
        "designersettings.cpp",
        "designersettings.h",
        "designmodecontext.cpp",
        "designmodecontext.h",
        "designmodewidget.cpp",
        "designmodewidget.h",
        "qmldesignerconstants.h",
        "qmldesignerplugin.cpp",
        "qmldesignerplugin.h",
        "settingspage.cpp",
        "settingspage.h",
        "settingspage.ui",
        "styledoutputpaneplaceholder.cpp",
        "styledoutputpaneplaceholder.h",
    ]
}
