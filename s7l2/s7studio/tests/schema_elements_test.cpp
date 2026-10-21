// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "main_window.hpp"
#include "diagram_editor.hpp"
#include "diagram/diagram_scene.hpp"
#include "schema_tabs.hpp"
#include "element_picker_panel.hpp"
#include "scene_serializer.hpp"
#include <s7/core.hpp>
#include <QApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QTabWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QTimer>
#include <QMessageBox>
#include <QAbstractButton>
#include <QPlainTextEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QUndoGroup>
#include <QUndoStack>
#include <fstream>
#include <iostream>

namespace {
void require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
class Window : public s7studio::MainWindow {
public:
    Window() : MainWindow(nullptr,false) {}
    using MainWindow::currentDocument;
    using MainWindow::documentModified;
    using MainWindow::applySchemaParamsToScene;
};
void click(s7studio::diagram::DiagramEditor* editor,const char* id,const char* type) {
    require(QMetaObject::invokeMethod(editor,"elementNodeDoubleClicked",Qt::DirectConnection,
        Q_ARG(QString,QString::fromUtf8(id)),Q_ARG(QString,QString::fromUtf8(type)),Q_ARG(int,0)),"Double-click signal dispatch");
}
boost::json::value model() {
    auto& registry=s7::ElementRegistry::instance();
    auto pi=*registry.find("PIController")->metadata().schema_definition;
    // Add a nested reusable graph to exercise multi-level editing/navigation.
    auto& body=pi.as_object();
    body.at("elements").as_array().push_back(boost::json::object{{"id","phase"},{"type","PhaseDifference"},
        {"schema",*registry.find("PhaseDifference")->metadata().schema_definition}});
    for(int port=0;port<2;++port)
        body.at("connections").as_array().push_back(boost::json::object{{"from_element","error"},{"from_port",0},{"to_element","phase"},{"to_port",port}});
    boost::json::array elements{
        boost::json::object{{"id","left"},{"type","PIController"},{"schema",pi}},
        boost::json::object{{"id","right"},{"type","PIController"},{"schema",*registry.find("PIController")->metadata().schema_definition}}};
    boost::json::array connections;
    const double values[]={1,.1,.2};
    for(int n=0;n<3;++n) {
        const auto id="input"+std::to_string(n);
        elements.push_back(boost::json::object{{"id",id},{"type","Constant"},{"params",{{"value",values[n]}}}});
        for(const char* component : {"left","right"})
            connections.push_back(boost::json::object{{"from_element",id},{"from_port",0},{"to_element",component},{"to_port",n}});
    }
    return boost::json::object{{"name","schema_tabs"},{"dt",.001},{"elements",elements},{"connections",connections}};
}
const boost::json::object& element(const boost::json::value& schema,const char* id) {
    for(const auto& value : schema.as_object().at("elements").as_array())
        if(value.as_object().at("id").as_string()==id) return value.as_object();
    throw std::runtime_error("Missing saved element");
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        QTemporaryDir temp;require(temp.isValid(),"Temporary settings directory");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        QApplication::setOrganizationName("s7-test");QApplication::setApplicationName("schema-tabs");
        auto& registry=s7::ElementRegistry::instance();s7::register_builtin_elements(registry);
        const auto original=*registry.find("PIController")->metadata().schema_definition;
        s7::SchemaLibraryDefinition user_library{"user_library", {{"UserPhaseDifference","User phase difference",
            "User-supplied schema resource","custom_category","1.0.0",
            *registry.find("PhaseDifference")->metadata().schema_definition}}};
        s7::SchemaLibraryLoader::register_library(user_library,registry);
        const auto input=temp.filePath("input.json");
        {std::ofstream file(input.toStdString());file<<boost::json::serialize(model());}
        bool unexpectedModal=false;
        QTimer modalGuard;
        QObject::connect(&modalGuard,&QTimer::timeout,&app,[&] {
            for(auto* widget : QApplication::topLevelWidgets())
                if(auto* box=qobject_cast<QMessageBox*>(widget); box && box->isVisible()) {
                    unexpectedModal=true;std::cerr<<"Unexpected dialog: "<<box->text().toStdString()<<'\n';box->reject();
                }
        });
        modalGuard.start(50);
        Window window;window.openPath(input);
        auto* tabs=window.findChild<QTabWidget*>("diagram.tabs");
        auto* manager=window.findChild<s7studio::SchemaTabs*>();
        require(tabs && manager && tabs->count()==1,"Root diagram tab");
        auto* root=qobject_cast<s7studio::diagram::DiagramEditor*>(tabs->widget(0));
        auto* picker=window.findChild<s7studio::ui::ElementPickerPanel*>();
        auto* tree=picker->findChild<QTreeWidget*>();
        int schema_count=0,primitive_count=0;
        for(int i=0;i<tree->topLevelItemCount();++i) {
            const auto* category=tree->topLevelItem(i);
            for(int j=0;j<category->childCount();++j) {
                const auto type=category->child(j)->data(0,Qt::UserRole).toString().toStdString();
                const auto* descriptor=registry.find(type);
                if(descriptor && descriptor->metadata().schema_definition) {
                    require(category->text(0)=="Schema elements","Schema entries have a separate category");++schema_count;
                }
                if(type=="ControlledIntegrator") ++primitive_count;
            }
        }
        require(schema_count>=6 && primitive_count==1,"Bundled resource and primitive picker entries");
        click(root,"left","PIController");require(tabs->count()==2,"Double-click opens a schema tab");
        auto* child=manager->activeEditor();require(child!=root,"Schema tab is a separate editor");
        tabs->setCurrentWidget(root);click(root,"left","PIController");require(tabs->count()==2,"Reopening selects existing instance tab");
        tabs->setCurrentWidget(root);click(root,"right","PIController");
        auto* right=manager->activeEditor();
        require(tabs->count()==3 && right!=child,"Different instances get independent tabs");
        require(QMetaObject::invokeMethod(tabs,"tabCloseRequested",Qt::DirectConnection,Q_ARG(int,tabs->indexOf(right))),"Close second instance");
        tabs->setCurrentWidget(child);
        // The global picker must insert into the current child, not the root.
        const auto root_count=root->scene()->elementNodes().size(), child_count=child->scene()->elementNodes().size();
        require(QMetaObject::invokeMethod(&window,"onElementRequested",Qt::DirectConnection,
            Q_ARG(QString,QStringLiteral("Constant")),Q_ARG(QString,QStringLiteral("Constant"))),"Active-tab insertion dispatch");
        require(root->scene()->elementNodes().size()==root_count && child->scene()->elementNodes().size()==child_count+1,"Element insertion follows active tab");
        manager->undoGroup()->undo();
        bool edited_properties=false;
        QTimer::singleShot(0,&window,[&] {
            for(auto* widget : QApplication::topLevelWidgets())
                if(auto* dialog=qobject_cast<QDialog*>(widget); dialog && dialog->windowTitle().startsWith("Element Properties")) {
                    auto* text=dialog->findChild<QPlainTextEdit*>();
                    auto* parameters = dialog->findChild<QTabWidget*>("element.parameterTabs");
                    auto* buttons = dialog->findChild<QDialogButtonBox*>();
                    if(text && parameters && buttons) {
                        parameters->setCurrentIndex(1);
                        text->setPlainText(QStringLiteral("{\"factor\":2}"));
                        edited_properties=true; buttons->button(QDialogButtonBox::Ok)->click();
                    }
                }
        });
        click(child,"error","Gain");
        require(edited_properties,"Primitive double-click still edits parameters inside a schema tab");
        require(element(root->scene()->elementNode("left")->element().schema,"error").at("params").as_object().at("factor")==2,"Child edit propagates to parent instance");
        require(root->scene()->elementNode("right")->element().schema==original,"Another instance remains independent");
        require(*registry.find("PIController")->metadata().schema_definition==original,"Registry resource remains immutable");
        manager->undoGroup()->undo();
        require(element(root->scene()->elementNode("left")->element().schema,"error").at("params").as_object().at("factor")==1,"Active-tab undo propagates");
        manager->undoGroup()->redo();
        click(child,"phase","PhaseDifference");require(tabs->count()==3,"Nested schema opens another tab");
        auto* grandchild=manager->activeEditor();auto wrap=grandchild->scene()->elementNode("wrap");
        auto edited=wrap->element();edited.params={{"period",4},{"lower",-2}};grandchild->scene()->changeElement(wrap,edited);
        const auto& left=root->scene()->elementNode("left")->element().schema;
        require(element(element(left,"phase").at("schema"),"wrap").at("params").as_object().at("period")==4,"Nested edit propagates through all parents");
        require(window.documentModified(),"Nested edits mark root document modified");
        const auto saved=window.currentDocument();
        auto computational=s7::SchemaLibraryLoader::build_schema(saved.as_object().at("schema"));
        s7::NodeRuntime runtime(s7::Compiler::compile(computational));
        runtime.step();require(std::abs(runtime.real_output_of("left")-.2)<1e-14 && std::abs(runtime.real_output_of("right")-.1)<1e-14,"Runtime executes edited instance rather than the registry template");
        require(element(left,"phase").at("schema").as_object().contains("exposed_inputs"),"Exposed interface survives edits");
        // Closing a parent closes its descendants; root stays open.
        require(QMetaObject::invokeMethod(tabs,"tabCloseRequested",Qt::DirectConnection,Q_ARG(int,tabs->indexOf(child))),"Close tab dispatch");
        require(tabs->count()==1,"Closing a schema tab also closes its descendant tabs");
        const auto output=temp.filePath("saved.s7diagram");
        {std::ofstream file(output.toStdString());file<<boost::json::serialize(saved);}
        QTimer::singleShot(0,[] {
            auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            require(box && box->windowTitle()=="Unsaved changes","Nested edits prompt before replacement");
            box->button(QMessageBox::Discard)->click();
        });
        window.openPath(output);require(tabs->count()==1,"Loading another document clears child tabs");
        click(root,"left","PIController");auto* reopened=manager->activeEditor();
        require(reopened->scene()->elementNode("error")->element().params.at("factor")==2,"Edits survive root save/reload");
        click(reopened,"phase","PhaseDifference");
        require(manager->activeEditor()->scene()->elementNode("wrap")->element().params.at("period")==4,"Nested edits and layout survive reload");
        tabs->setCurrentWidget(root);
        // Root undo is reflected when a child is reopened, rather than overwritten.
        root->scene()->undoStack()->clear();
        auto node=root->scene()->elementNode("left");auto replacement=node->element();replacement.schema=original;
        root->scene()->changeElement(node,replacement);click(root,"left","PIController");
        require(manager->activeEditor()->scene()->elementNode("error")->element().params.at("factor")==1,"Tab refreshes after parent replacement");
        tabs->setCurrentWidget(root);root->scene()->undoStack()->undo();click(root,"left","PIController");
        require(manager->activeEditor()->scene()->elementNode("error")->element().params.at("factor")==2,"Parent undo refreshes an open schema tab");
        require(!unexpectedModal,"Schema editing must not raise an unexpected modal error");
        std::cout<<"Studio schema elements: category, double-click tabs, nested edits, undo, isolation, save/reload and execution passed\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
