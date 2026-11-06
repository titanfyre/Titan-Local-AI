#include "runtime/ModelCatalog.h"
#include "runtime/RuntimeManager.h"
#include "LocalApiClient.h"
#include <QtTest>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkProxy>
#include <QSaveFile>
using namespace titan;
class RuntimeTests : public QObject {
 Q_OBJECT
private:
 void gguf(const QString &path, const QString &templ, int shardCount = 1) {
  QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("GGUF", 4);
  QDataStream out(&file); out.setByteOrder(QDataStream::LittleEndian);
  out << quint32(3) << quint64(1) << quint64(5);
  auto str = [&](const QString &s) { const auto b=s.toUtf8(); out << quint64(b.size()); file.write(b); };
  str("general.name"); out << quint32(8); str("Small test model");
  str("general.architecture"); out << quint32(8); str("llama");
  str("general.file_type"); out << quint32(4) << quint32(15);
  str("tokenizer.chat_template"); out << quint32(8); str(templ);
  str("split.count"); out << quint32(2) << quint16(shardCount);
 }
private slots:
 void initTestCase() {
  QCoreApplication::setOrganizationName("TitanLocalAITests");
  QCoreApplication::setApplicationName("RuntimeTests");
  QStandardPaths::setTestModeEnabled(true);
 }
 void init() {
  QTest::failOnWarning(QRegularExpression(QStringLiteral("QProcess: Destroyed while process.*")));
 }
 void officialRuntimeInstallationAndCache() {
  if (qEnvironmentVariable("TITAN_TEST_INSTALL_RUNTIME") != "1")
   QSKIP("Set TITAN_TEST_INSTALL_RUNTIME=1 to download and verify the pinned official runtime.");
  RuntimeManager runtime;
  runtime.configure(QVariantMap{{"gpuLayers", 0}});
  QSignalSpy failure(&runtime,&RuntimeManager::failed);
  runtime.ensureInstalled();
  QTRY_VERIFY_WITH_TIMEOUT(runtime.progress() == 1 || !failure.isEmpty(),180000);
  QVERIFY2(failure.isEmpty(), qPrintable(runtime.status()));
  QVERIFY(!runtime.ready()); QVERIFY(runtime.apiKey().isEmpty());
  RuntimeManager cached; cached.configure(QVariantMap{{"gpuLayers", 0}});
  cached.ensureInstalled();
  QTRY_VERIFY_WITH_TIMEOUT(cached.progress() == 1,10000);
 }
 void managedInference_data() {
  QTest::addColumn<int>("gpuLayers");
  QTest::newRow("cpu") << 0;
  QTest::newRow("automatic") << -1;
 }
 void managedInference() {
  QFETCH(int,gpuLayers);
  const QString modelPath = qEnvironmentVariable("TITAN_TEST_MODEL_PATH");
  if (modelPath.isEmpty()) QSKIP("Set TITAN_TEST_MODEL_PATH to an existing local instruct GGUF for managed inference validation.");
  const QFileInfo modelFile(modelPath); QVERIFY(modelFile.isFile());
  ModelCatalog catalog; catalog.setDirectory(modelFile.absolutePath());
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.scanning(),10000);
  QVariantMap model;
  for (const auto &value : catalog.models())
   if (QFileInfo(value.toMap().value("path").toString()).canonicalFilePath() == modelFile.canonicalFilePath()) model=value.toMap();
  QVERIFY2(!model.isEmpty(),qPrintable(catalog.scanWarning()));
  RuntimeManager runtime; runtime.configure(QVariantMap{{"provider","managed"},{"gpuLayers",gpuLayers},{"contextSize",2048},{"threads",0}});
  QSignalSpy runtimeFailure(&runtime,&RuntimeManager::failed); QSignalSpy ready(&runtime,&RuntimeManager::modelReady);
  QString lastStatus;
  connect(&runtime,&RuntimeManager::stateChanged,this,[&] {
   if (lastStatus != runtime.status()) { lastStatus=runtime.status(); qInfo().noquote() << "Runtime:" << lastStatus; }
  });
  QElapsedTimer startup; startup.start(); runtime.loadModel(model);
  QTRY_VERIFY_WITH_TIMEOUT(runtime.ready() || !runtimeFailure.isEmpty(),180000);
  QVERIFY2(runtimeFailure.isEmpty(),qPrintable(runtime.status()+"\n"+runtime.logTail()));
  QCOMPARE(ready.count(),1); QVERIFY(!runtime.apiKey().isEmpty()); QVERIFY(!runtime.logTail().contains(runtime.apiKey()));
  const qint64 startupMs=startup.elapsed();
  const QUrl endpoint(runtime.baseUrl()); QCOMPARE(endpoint.host(),QString("127.0.0.1"));
  // An unauthenticated process cannot use the private managed inference endpoint.
  QNetworkAccessManager unauthenticated; unauthenticated.setProxy(QNetworkProxy::NoProxy);
  QNetworkRequest request(QUrl(runtime.baseUrl()+"/models")); request.setTransferTimeout(5000);
  auto reply=unauthenticated.get(request); QSignalSpy requestFinished(reply,&QNetworkReply::finished);
  QTRY_VERIFY_WITH_TIMEOUT(!requestFinished.isEmpty(),10000);
  QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),401); reply->deleteLater();
  LocalApiClient client; QSignalSpy deltas(&client,&LocalApiClient::textDelta);
  QSignalSpy complete(&client,&LocalApiClient::completed); QSignalSpy apiFailure(&client,&LocalApiClient::failed);
  QString text; connect(&client,&LocalApiClient::textDelta,this,[&](const QString &delta) { text+=delta; });
  const QString prompt=QStringLiteral("Say hello in one short sentence.");
  const QJsonArray messages{QJsonObject{{"role","system"},{"content","You are a concise helpful assistant."}},
   QJsonObject{{"role","user"},{"content",prompt}}};
  QElapsedTimer generation; generation.start();
  client.request(endpoint,QJsonObject{{"model",runtime.modelId()},{"messages",messages},{"max_tokens",32},{"temperature",0}},runtime.apiKey());
  QTRY_VERIFY_WITH_TIMEOUT(!complete.isEmpty() || !apiFailure.isEmpty(),60000);
  QVERIFY2(apiFailure.isEmpty(),apiFailure.isEmpty() ? "" : qPrintable(apiFailure.first().first().toString()));
  QCOMPARE(complete.count(),1); QVERIFY(deltas.count()>0); QVERIFY(!text.trimmed().isEmpty());
  QCOMPARE(complete.first().first().toJsonObject().value("content").toString(),text);
  qInfo().noquote() << "Managed response:" << text << "(" << deltas.count() << "streamed deltas)";
  const QString evidenceDirectory=qEnvironmentVariable("TITAN_TEST_EVIDENCE_DIRECTORY");
  if (!evidenceDirectory.isEmpty()) {
   QVERIFY(QDir().mkpath(evidenceDirectory));
   const QJsonObject evidence{{"backendRequested",gpuLayers == 0 ? "cpu" : "automatic"},{"model",modelFile.canonicalFilePath()},
    {"modelId",runtime.modelId()},{"startupMs",startupMs},{"generationMs",generation.elapsed()},
    {"textDeltaCount",deltas.count()},{"prompt",prompt},{"response",text},{"unauthenticatedHttpStatus",401},{"serverLog",runtime.logTail()}};
   QSaveFile output(QDir(evidenceDirectory).filePath(gpuLayers == 0 ? "managed-cpu.json" : "managed-automatic.json"));
   QVERIFY(output.open(QIODevice::WriteOnly)); QVERIFY(output.write(QJsonDocument(evidence).toJson())>0); QVERIFY(output.commit());
  }
  runtime.stop(); QVERIFY(!runtime.ready()); QVERIFY(runtime.apiKey().isEmpty());
  QTcpServer reclaimed;
  QTRY_VERIFY_WITH_TIMEOUT(reclaimed.isListening() || reclaimed.listen(QHostAddress::LocalHost,quint16(endpoint.port())),5000);
 }
 void managedDestructionWhileReady_data() {
  QTest::addColumn<int>("gpuLayers"); QTest::newRow("cpu") << 0; QTest::newRow("automatic") << -1;
 }
 void managedDestructionWhileReady() {
  QFETCH(int,gpuLayers);
  const QString path=qEnvironmentVariable("TITAN_TEST_MODEL_PATH");
  if(path.isEmpty()) QSKIP("Set TITAN_TEST_MODEL_PATH to validate immediate managed-runtime destruction.");
  auto runtime=std::make_unique<RuntimeManager>();
  runtime->configure(QVariantMap{{"gpuLayers",gpuLayers},{"contextSize",2048}});
  QSignalSpy failure(runtime.get(),&RuntimeManager::failed);
  runtime->loadModel(QVariantMap{{"path",path},{"id","shutdown-test"},{"name","Shutdown test model"}});
  QTRY_VERIFY_WITH_TIMEOUT(runtime->ready() || !failure.isEmpty(),180000);
  QVERIFY2(failure.isEmpty(),qPrintable(runtime->status()));
  const int port=QUrl(runtime->baseUrl()).port();
  // Do not stop first: destruction must reap both active and retired children.
  runtime.reset();
  QTcpServer reclaimed; QVERIFY(reclaimed.listen(QHostAddress::LocalHost,quint16(port)));
 }
 void managedCancelledInstallResumesPendingLoad() {
  const QString path=qEnvironmentVariable("TITAN_TEST_MODEL_PATH");
  if(path.isEmpty()) QSKIP("Set TITAN_TEST_MODEL_PATH to validate load retry after a cancelled runtime worker.");
  RuntimeManager runtime; runtime.configure(QVariantMap{{"gpuLayers",0},{"contextSize",2048}});
  runtime.loadModel(QVariantMap{{"path",path},{"id","superseded"},{"name","Superseded model"}});
  runtime.stop();
  QSignalSpy failure(&runtime,&RuntimeManager::failed);
  runtime.loadModel(QVariantMap{{"path",path},{"id","latest"},{"name","Latest model"}});
  QTRY_VERIFY_WITH_TIMEOUT(runtime.ready() || !failure.isEmpty(),180000);
  QVERIFY2(failure.isEmpty(),qPrintable(runtime.status())); QCOMPARE(runtime.modelId(),QString("latest"));
 }
 void managedThinkingAndNativeToolRoundTrip() {
  const QString path=qEnvironmentVariable("TITAN_TEST_REASONING_MODEL_PATH");
  if(path.isEmpty()) QSKIP("Set TITAN_TEST_REASONING_MODEL_PATH to a local Qwen3 GGUF for thinking/native-tool validation.");
  ModelCatalog catalog; catalog.setDirectory(QFileInfo(path).absolutePath());
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.scanning(),10000);
  QVariantMap model;
  for(const auto &entry:catalog.models())
   if(QFileInfo(entry.toMap().value("path").toString()).canonicalFilePath()==QFileInfo(path).canonicalFilePath()) model=entry.toMap();
  QVERIFY(!model.isEmpty()); QVERIFY(model.value("supportsThinking").toBool());
  RuntimeManager runtime; runtime.configure(QVariantMap{{"gpuLayers",-1},{"contextSize",4096}});
  QSignalSpy runtimeFailure(&runtime,&RuntimeManager::failed); runtime.loadModel(model);
  QTRY_VERIFY_WITH_TIMEOUT(runtime.ready() || !runtimeFailure.isEmpty(),180000);
  QVERIFY2(runtimeFailure.isEmpty(),qPrintable(runtime.status()));
  QTRY_COMPARE_WITH_TIMEOUT(runtime.thinkingStyle(),QString("enable_thinking"),10000);
  QCOMPARE(runtime.thinkingLevels(),QStringList({"auto","on","off"}));
  LocalApiClient client;
  auto send=[&](QJsonObject payload,int &textDeltas,int &reasoningDeltas) {
   QSignalSpy complete(&client,&LocalApiClient::completed), failure(&client,&LocalApiClient::failed);
   QSignalSpy text(&client,&LocalApiClient::textDelta), reasoning(&client,&LocalApiClient::reasoningDelta);
   payload["model"]=runtime.modelId(); payload["temperature"]=0.6; payload["top_p"]=0.95;
   payload["top_k"]=20; payload["seed"]=42; payload["max_tokens"]=1024;
   client.request(QUrl(runtime.baseUrl()),payload,runtime.apiKey());
   QElapsedTimer deadline; deadline.start();
   while(complete.isEmpty() && failure.isEmpty() && deadline.elapsed()<90000) QTest::qWait(20);
   textDeltas=text.count(); reasoningDeltas=reasoning.count();
   if(!failure.isEmpty()) return QJsonObject{{"test_error",failure.first().first().toString()}};
   if(complete.isEmpty()) { client.cancel(); return QJsonObject{{"test_error","Timed out waiting for a real model response."}}; }
   return complete.first().first().toJsonObject();
  };
  const QJsonArray question{QJsonObject{{"role","user"},{"content","What is 2 + 2? Think briefly and answer in one sentence."}}};
  int offText=0,offReasoning=0,onText=0,onReasoning=0;
  const auto off=send(QJsonObject{{"messages",question},{"chat_template_kwargs",QJsonObject{{"enable_thinking",false}}}},offText,offReasoning);
  QVERIFY2(!off.contains("test_error"),qPrintable(off.value("test_error").toString()));
  QVERIFY(!off.value("content").toString().isEmpty()); QCOMPARE(offReasoning,0);
  const auto on=send(QJsonObject{{"messages",question},{"chat_template_kwargs",QJsonObject{{"enable_thinking",true}}}},onText,onReasoning);
  QVERIFY2(!on.contains("test_error"),qPrintable(on.value("test_error").toString()));
  QVERIFY(onReasoning>0); QVERIFY(!on.value("reasoning_content").toString().isEmpty()); QVERIFY(!on.value("content").toString().isEmpty());
  const QJsonObject definition{{"type","function"},{"function",QJsonObject{{"name","validation_echo"},
   {"description","Return the supplied probe value unchanged."},{"parameters",QJsonObject{{"type","object"},
    {"properties",QJsonObject{{"value",QJsonObject{{"type","string"},{"enum",QJsonArray{"titan_probe"}}}}}},
    {"required",QJsonArray{"value"}},{"additionalProperties",false}}}}}};
  const QJsonArray tools{definition};
  QJsonArray messages{QJsonObject{{"role","user"},{"content","Call validation_echo with titan_probe. After receiving its result, reply with the echo value."}}};
  int toolText=0,toolReasoning=0;
  const auto first=send(QJsonObject{{"messages",messages},{"tools",tools},
   {"tool_choice","required"},
   {"chat_template_kwargs",QJsonObject{{"enable_thinking",false}}}},toolText,toolReasoning);
  QVERIFY2(!first.contains("test_error"),qPrintable(first.value("test_error").toString()));
  const auto calls=first.value("tool_calls").toArray(); QCOMPARE(calls.size(),1);
  const auto call=calls.first().toObject(), function=call.value("function").toObject();
  QCOMPARE(function.value("name").toString(),QString("validation_echo"));
  const auto arguments=QJsonDocument::fromJson(function.value("arguments").toString().toUtf8()).object();
  QCOMPARE(arguments.value("value").toString(),QString("titan_probe"));
  // Execute the declared echo operation locally and send its actual result back.
  const QJsonObject toolResult{{"ok",true},{"echo",arguments.value("value")}};
  messages.append(QJsonObject{{"role","assistant"},{"content",first.value("content")},{"tool_calls",calls}});
  messages.append(QJsonObject{{"role","tool"},{"tool_call_id",call.value("id")},{"name","validation_echo"},
   {"content",QString::fromUtf8(QJsonDocument(toolResult).toJson(QJsonDocument::Compact))}});
  int finalText=0,finalReasoning=0;
  const auto final=send(QJsonObject{{"messages",messages},{"tools",tools},{"tool_choice","none"},
   {"chat_template_kwargs",QJsonObject{{"enable_thinking",false}}}},finalText,finalReasoning);
  QVERIFY2(!final.contains("test_error"),qPrintable(final.value("test_error").toString()));
  QVERIFY(!final.value("content").toString().isEmpty()); QVERIFY(final.value("tool_calls").toArray().isEmpty());
  const QString evidenceDirectory=qEnvironmentVariable("TITAN_TEST_EVIDENCE_DIRECTORY");
  if(!evidenceDirectory.isEmpty()) {
   QVERIFY(QDir().mkpath(evidenceDirectory)); QSaveFile output(QDir(evidenceDirectory).filePath("qwen-thinking-tools.json"));
   const QJsonObject evidence{{"model",QFileInfo(path).canonicalFilePath()},{"thinkingStyle",runtime.thinkingStyle()},
    {"thinkingLevels",QJsonArray::fromStringList(runtime.thinkingLevels())},{"thinkingOffReasoningDeltas",offReasoning},
    {"thinkingOnReasoningDeltas",onReasoning},{"thinkingOffResponse",off.value("content")},{"thinkingOnResponse",on.value("content")},
    {"toolCall",call},{"toolResult",toolResult},{"finalResponse",final.value("content")},{"serverLog",runtime.logTail()}};
   QVERIFY(output.open(QIODevice::WriteOnly)); QVERIFY(output.write(QJsonDocument(evidence).toJson())>0); QVERIFY(output.commit());
  }
 }
 void catalogMetadataAndSplits() {
  QTemporaryDir dir; QVERIFY(dir.isValid());
  gguf(dir.filePath("small.gguf"), "{% if enable_thinking %}<think>{% endif %}");
  gguf(dir.filePath("split-00001-of-00002.gguf"), "{{ messages }}", 2);
  QFile invalid(dir.filePath("invalid.gguf")); QVERIFY(invalid.open(QIODevice::WriteOnly)); invalid.write("not a model"); invalid.close();
  ModelCatalog catalog; catalog.setDirectory(dir.path());
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.scanning(), 5000);
  QCOMPARE(catalog.models().size(), 2);
  bool seenPlain=false, seenSplit=false;
  for (const auto &v : catalog.models()) {
   const auto m=v.toMap(); QCOMPARE(m.value("architecture").toString(), "llama");
   QCOMPARE(m.value("quantization").toString(), "Q4_K_M");
   if (m.value("path").toString().endsWith("small.gguf")) { seenPlain=true; QVERIFY(m.value("supportsThinking").toBool()); QVERIFY(m.value("loadable").toBool()); }
   else { seenSplit=true; QVERIFY(!m.value("supportsThinking").toBool()); QVERIFY(!m.value("loadable").toBool()); }
  }
  QVERIFY(seenPlain && seenSplit);
  QFile shard(dir.filePath("split-00002-of-00002.gguf")); QVERIFY(shard.open(QIODevice::WriteOnly)); shard.write("GGUF"); shard.close();
  catalog.refresh(); QTRY_VERIFY_WITH_TIMEOUT(!catalog.scanning(), 5000);
  for (const auto &v : catalog.models()) QVERIFY(v.toMap().value("loadable").toBool());
 }
 void catalogRejectsMalformedAndAuxiliaryFiles() {
  QTemporaryDir directory; QVERIFY(directory.isValid());
  auto header=[](quint32 version,quint64 count) {
   QByteArray bytes; QDataStream stream(&bytes,QIODevice::WriteOnly); stream.setByteOrder(QDataStream::LittleEndian);
   stream.writeRawData("GGUF",4); stream << version << quint64(1) << count; return bytes;
  };
  auto write=[&](const QString &name,const QByteArray &bytes) {
   QFile file(directory.filePath(name)); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes),bytes.size());
  };
  write("truncated.gguf",QByteArray("GGUF"));
  write("unsupported-version.gguf",header(1,0));
  write("too-many-metadata-keys.gguf",header(3,~quint64(0)));
  auto oversized=header(3,1); QDataStream length(&oversized,QIODevice::Append); length.setByteOrder(QDataStream::LittleEndian); length << quint64(1ULL<<40);
  write("oversized-string.gguf",oversized);
  gguf(directory.filePath("mmproj-vision.gguf"),"enable_thinking");
  gguf(directory.filePath("not-a-supported-format.safetensors"),"enable_thinking");
  gguf(directory.filePath("gpt-oss-thinking-name-is-not-capability.gguf"),"{{ messages }}");
  ModelCatalog catalog; catalog.setDirectory(directory.path()); QTRY_VERIFY_WITH_TIMEOUT(!catalog.scanning(),5000);
  QCOMPARE(catalog.models().size(),1); QVERIFY(!catalog.models().first().toMap().value("supportsThinking").toBool());
  QVERIFY(!catalog.scanWarning().isEmpty());
 }
 void catalogWatchesAdditionsAndRemovals() {
  QTemporaryDir directory; QVERIFY(directory.isValid());
  ModelCatalog catalog; catalog.setDirectory(directory.path()); QTRY_VERIFY_WITH_TIMEOUT(!catalog.scanning(),5000);
  QVERIFY(catalog.models().isEmpty());
  gguf(directory.filePath("watched.gguf"),"{{ messages }}");
  QTRY_COMPARE_WITH_TIMEOUT(catalog.models().size(),1,5000);
  QVERIFY(QFile::remove(directory.filePath("watched.gguf")));
  QTRY_COMPARE_WITH_TIMEOUT(catalog.models().size(),0,5000);
 }
 void ollamaThinkingMetadata_data() {
  QTest::addColumn<QByteArray>("metadata"); QTest::addColumn<QString>("style");
  QTest::addColumn<QStringList>("levels"); QTest::addColumn<bool>("supports");
  QTest::newRow("named-effort") << QByteArray("{\"thinking\":{\"values\":[\"low\",\"medium\",\"high\"],\"default\":\"medium\"},\"capabilities\":[\"thinking\"]}") << QString("reasoning_effort") << QStringList({"auto","low","medium","high"}) << true;
  QTest::newRow("boolean") << QByteArray("{\"thinking\":{\"values\":[true,false],\"default\":true},\"capabilities\":[\"thinking\"]}") << QString("ollama") << QStringList({"auto","on","off"}) << true;
  QTest::newRow("disabled-authoritative") << QByteArray("{\"thinking\":{\"values\":[false],\"default\":false},\"template\":\"reasoning_effort\",\"capabilities\":[\"thinking\"]}") << QString("none") << QStringList({"auto"}) << false;
  QTest::newRow("effort-template-preserved") << QByteArray("{\"template\":\"{{ reasoning_effort }}\",\"capabilities\":[\"thinking\"]}") << QString("reasoning_effort") << QStringList({"auto","low","medium","high"}) << true;
  QTest::newRow("capability-only") << QByteArray("{\"capabilities\":[\"thinking\"]}") << QString("ollama") << QStringList({"auto"}) << true;
  QTest::newRow("no-evidence") << QByteArray("{\"capabilities\":[\"completion\"]}") << QString("none") << QStringList({"auto"}) << false;
  QTest::newRow("advertised-subset") << QByteArray("{\"thinking\":{\"values\":[\"low\",\"high\"],\"default\":\"high\"},\"capabilities\":[\"thinking\"]}") << QString("reasoning_effort") << QStringList({"auto","low","high"}) << true;
 }
 void ollamaThinkingMetadata() {
  QFETCH(QByteArray,metadata); QFETCH(QString,style); QFETCH(QStringList,levels); QFETCH(bool,supports);
  RuntimeManager runtime; runtime.configure(QVariantMap{{"provider","ollama"}});
  QSignalSpy ready(&runtime,&RuntimeManager::modelReady); QSignalSpy failure(&runtime,&RuntimeManager::failed);
  QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost,0));
  int showRequests=0;
  connect(&server,&QTcpServer::newConnection,this,[&] {
   auto socket=server.nextPendingConnection();
   connect(socket,&QTcpSocket::readyRead,socket,[&,socket] {
    const auto request=socket->readAll(); QByteArray body;
    if(request.startsWith("GET /api/tags")) body="{\"models\":[{\"model\":\"local:latest\"},{\"model\":\"remote:cloud\"},{\"model\":\"big:120b-cloud\"},{\"model\":\"masked:latest\",\"remote_host\":\"https://ollama.com\"},{\"model\":\"my-cloud-finetune:latest\"}]}";
    else { QVERIFY(request.startsWith("POST /api/show")); QVERIFY(!runtime.ready()); ++showRequests; body=metadata; }
    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body); socket->disconnectFromHost();
   }); connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
  });
  runtime.setExternalEndpoint(QString("http://127.0.0.1:%1/v1").arg(server.serverPort()),"local:latest");
  QTRY_VERIFY_WITH_TIMEOUT(runtime.ready() || !failure.isEmpty(),5000);
  QVERIFY2(failure.isEmpty(),qPrintable(runtime.status())); QCOMPARE(ready.count(),1); QCOMPARE(showRequests,1);
  QCOMPARE(runtime.supportsThinking(),supports); QCOMPARE(runtime.thinkingStyle(),style); QCOMPARE(runtime.thinkingLevels(),levels);
  QCOMPARE(runtime.externalModels().size(),2);
 }
 void ollamaCloudRejection_data() {
  QTest::addColumn<QString>("selected"); QTest::addColumn<QByteArray>("tags"); QTest::addColumn<QByteArray>("show"); QTest::addColumn<int>("expectedShows");
  QTest::newRow("cloud-tag") << QString("big:cloud") << QByteArray("{}") << QByteArray("{}") << 0;
  QTest::newRow("cloud-size-tag") << QString("big:120b-cloud") << QByteArray("{}") << QByteArray("{}") << 0;
  QTest::newRow("remote-tags") << QString("masked:latest") << QByteArray("{\"models\":[{\"model\":\"masked:latest\",\"remote_host\":\"https://ollama.com\"}]}") << QByteArray("{}") << 0;
  QTest::newRow("remote-show") << QString("masked:latest") << QByteArray("{\"models\":[{\"model\":\"masked:latest\"}]}") << QByteArray("{\"remote_host\":\"https://ollama.com\",\"capabilities\":[\"thinking\"]}") << 1;
 }
 void ollamaCloudRejection() {
  QFETCH(QString,selected); QFETCH(QByteArray,tags); QFETCH(QByteArray,show); QFETCH(int,expectedShows);
  RuntimeManager runtime; runtime.configure(QVariantMap{{"provider","ollama"}});
  QSignalSpy ready(&runtime,&RuntimeManager::modelReady); QSignalSpy failure(&runtime,&RuntimeManager::failed);
  QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost,0)); int showRequests=0;
  connect(&server,&QTcpServer::newConnection,this,[&] {
   auto socket=server.nextPendingConnection(); connect(socket,&QTcpSocket::readyRead,socket,[&,socket] {
    const auto request=socket->readAll(); QByteArray body=tags;
    if(request.startsWith("POST /api/show")) { ++showRequests; body=show; }
    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body); socket->disconnectFromHost();
   }); connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
  });
  runtime.setExternalEndpoint(QString("http://127.0.0.1:%1/v1").arg(server.serverPort()),selected);
  QTRY_VERIFY_WITH_TIMEOUT(!failure.isEmpty(),5000); QVERIFY(!runtime.ready()); QCOMPARE(ready.count(),0); QCOMPARE(showRequests,expectedShows); QCOMPARE(runtime.externalModels().size(),0);
 }
 void rejectsRemoteEndpoints() {
  RuntimeManager runtime; QSignalSpy failed(&runtime, &RuntimeManager::failed);
  runtime.setExternalEndpoint("http://example.com:11434", "model"); QCOMPARE(failed.count(),1); QVERIFY(!runtime.ready());
  runtime.setExternalEndpoint("http://127.0.0.1.evil.example/v1", "model"); QCOMPARE(failed.count(),2);
  runtime.setExternalEndpoint("http://user:pass@127.0.0.1:1234", "model"); QCOMPARE(failed.count(),3);
  QVERIFY(runtime.apiKey().isEmpty());
 }
 void localProviderAndCapabilities() {
  QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost,0));
  connect(&server,&QTcpServer::newConnection,this,[&] {
   auto socket=server.nextPendingConnection();
   connect(socket,&QTcpSocket::readyRead,socket,[socket] {
    const auto request=socket->readAll();
    const QByteArray body=request.startsWith("GET /v1/models") ? QByteArray("{\"data\":[{\"id\":\"tiny\"}]}") : QByteArray("{\"chat_template\":\"{% if enable_thinking %}<think>{% endif %}\"}");
    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body); socket->disconnectFromHost();
   });
   connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
  });
  RuntimeManager runtime;
  runtime.setExternalEndpoint(QString("http://localhost:%1/v1/").arg(server.serverPort()));
  QTRY_VERIFY_WITH_TIMEOUT(runtime.ready(), 5000);
  QCOMPARE(runtime.modelId(),"tiny"); QCOMPARE(runtime.externalModels().size(),1);
  QTRY_VERIFY_WITH_TIMEOUT(runtime.supportsThinking(),5000); QCOMPARE(runtime.thinkingStyle(),"enable_thinking");
  QVERIFY(runtime.baseUrl().startsWith("http://127.0.0.1:")); QVERIFY(runtime.apiKey().isEmpty());
  runtime.stop(); QVERIFY(!runtime.ready()); QVERIFY(server.isListening());
  QVERIFY(runtime.externalModels().isEmpty()); QVERIFY(runtime.modelId().isEmpty());
 }
};
QTEST_GUILESS_MAIN(RuntimeTests)
#include "RuntimeTests.moc"
