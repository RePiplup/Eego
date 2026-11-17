#include <QtTest/QTest>
#include <QQuickWindow>
#include <QQuickItem>
#include <QJSValue>
#include <QElapsedTimer>
#include <QFontDatabase>
int main(int argc,char**argv){
 std::cout<<"START QApplication"<<std::endl;QApplication app(argc,argv);std::cout<<"START service"<<std::endl;if(argc!=4)return 2;
 QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
 QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyhbd.ttc");
 QQuickStyle::setStyle("Basic");
 DownloadService service(QString::fromLocal8Bit(argv[1]));
 QString errors;QObject::connect(&service,&DownloadService::problem,[&](QString s){errors+=s+'\n';});
 QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("downloadService",&service);
 std::cout<<"START QML"<<std::endl;engine.load(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[2])));std::cout<<"LOADED QML"<<std::endl;if(engine.rootObjects().isEmpty())return 3;
 auto win=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]);win->resize(1060,1200);win->show();
 auto list=win->findChild<QQuickItem*>("taskList");auto menu=win->findChild<QObject*>("taskMenu");auto dialog=win->findChild<QObject*>("removeDialog");auto search=win->findChild<QObject*>("taskSearch");
 QString out=QString::fromLocal8Bit(argv[3]);QDir().mkpath(out);
 auto require=[&](bool ok,const char*label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<std::endl;if(!ok){win->grabWindow().save(out+"/failure.png");std::exit(10);}};
 auto ids=[&](){return win->property("selectedIds").value<QJSValue>().toVariant().toList();};
 auto click=[&](int row,Qt::MouseButton button=Qt::LeftButton,Qt::KeyboardModifiers mods=Qt::NoModifier){QTest::mouseClick(win,button,mods,list->mapToScene(QPointF(80,row*94+40)).toPoint());QTest::qWait(80);};
 auto closeMenu=[&](){QMetaObject::invokeMethod(menu,"close");QTest::qWait(80);};
 auto snap=[&](QString name){require(win->grabWindow().save(out+'/'+name+".png"),"save screenshot");};
 service.start();QTest::qWait(1800);require(list&&menu&&dialog&&search,"QML controls loaded");require(list->property("count").toInt()==6,"real isolated host exposes six seeded tasks");
 auto synthetic=QString::fromLocal8Bit(argv[1])+"/Downloads/1-alpha.txt";
 require(QFile::rename(synthetic,synthetic+".test-held"),"temporarily hide synthetic fixture");
 service.openPath(1,false);require(errors.contains(QString::fromUtf8("文件或文件夹已不存在："))&&errors.contains("1-alpha.txt"),"missing file reports exact path");
 require(QFile::rename(synthetic+".test-held",synthetic),"restore synthetic fixture");errors.clear();
 click(0);require(ids()==QVariantList{1},"single selection");
 click(1,Qt::LeftButton,Qt::ControlModifier);require(ids()==QVariantList({1,2}),"Ctrl adds second task");
 click(1,Qt::RightButton);require(ids()==QVariantList({1,2})&&menu->property("opened").toBool(),"right click selected preserves multi-selection");snap("01-multi-context");closeMenu();
 QTest::qWait(1000);require(ids()==QVariantList({1,2}),"poll refresh preserves stable IDs");
 click(3,Qt::LeftButton,Qt::ShiftModifier);require(ids()==QVariantList({2,3,4}),"Shift range uses last anchor");
 click(4,Qt::RightButton);require(ids()==QVariantList{5},"right click unselected selects only target");closeMenu();
 auto start=list->mapToScene(QPointF(15,12)).toPoint(),end=list->mapToScene(QPointF(300,260)).toPoint();
 QTest::mousePress(win,Qt::LeftButton,Qt::NoModifier,start);QTest::mouseMove(win,end,60);QTest::mouseRelease(win,Qt::LeftButton,Qt::NoModifier,end);QTest::qWait(100);
 require(ids()==QVariantList({1,2,3}),"rubber-band selects intersecting rows");snap("02-rubber-band-result");
 list->forceActiveFocus();QTest::keyClick(win,Qt::Key_Delete);QTest::qWait(100);require(dialog->property("opened").toBool(),"Delete opens confirmation");
 require(win->property("removeTargets").value<QJSValue>().toVariant().toList().size()==3,"confirmation freezes three selected IDs");snap("03-delete-confirmation");
 QTest::keyClick(win,Qt::Key_Escape);QTest::qWait(100);require(!dialog->property("opened").toBool()&&list->property("count").toInt()==6,"Escape cancels without removal");
 list->forceActiveFocus();QTest::keyClick(win,Qt::Key_Delete);QTest::qWait(100);QTest::keyClick(win,Qt::Key_Return);QTest::qWait(100);require(!dialog->property("opened").toBool()&&list->property("count").toInt()==6,"default focused Enter cancels without removal");
 search->setProperty("text","charlie");QTest::qWait(150);require(ids()==QVariantList{3}&&list->property("count").toInt()==1,"filter prunes hidden selected IDs");
 search->setProperty("text","");QTest::qWait(100);require(ids()==QVariantList{3},"clearing filter does not resurrect hidden selection");
 QTest::mouseClick(win,Qt::RightButton,Qt::NoModifier,list->mapToScene(QPointF(80,650)).toPoint());QTest::qWait(100);require(ids().isEmpty()&&menu->property("opened").toBool(),"blank context clears selection");snap("04-blank-context");closeMenu();
 list->forceActiveFocus();QTest::keyClick(win,Qt::Key_A,Qt::ControlModifier);require(ids().size()==6,"Ctrl+A selects visible tasks");QTest::keyClick(win,Qt::Key_C,Qt::ControlModifier);require(QApplication::clipboard()->text().split('\n').size()==6,"Ctrl+C copies selected links");
 QTest::keyClick(win,Qt::Key_Escape);require(ids().isEmpty(),"Escape clears selection");
 click(0);QTest::keyClick(win,Qt::Key_Down,Qt::ShiftModifier);require(ids()==QVariantList({1,2}),"Shift+Down extends selection");
 // Exercise the same confirmation button as the live application with disposable fixture records.
 QTest::keyClick(win,Qt::Key_Delete);QTest::qWait(100);
 QQuickItem* confirm=nullptr;
 std::function<void(QQuickItem*)> visit=[&](QQuickItem* item){if(item->property("text").toString()==QString::fromUtf8("仅移除任务，保留文件")&&item->isVisible())confirm=item;for(auto c:item->childItems())visit(c);};visit(win->contentItem());
 require(confirm,"record-only confirmation button exists");
 QTest::mouseClick(win,Qt::LeftButton,Qt::NoModifier,confirm->mapToScene(QPointF(confirm->width()/2,confirm->height()/2)).toPoint());QTest::qWait(1600);
 require(list->property("count").toInt()==4&&ids().isEmpty(),"confirm removes only two tasks and prunes selection");require(errors.isEmpty(),"no service errors");snap("05-after-removal");
 win->resize(720,600);QTest::qWait(150);snap("06-narrow-layout");
 service.shutdown();QTimer::singleShot(10000,&app,&QCoreApplication::quit);return app.exec();
}
