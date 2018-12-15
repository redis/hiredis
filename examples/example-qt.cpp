#include "example-qt.h"
#include <QCoreApplication>
#include <QTimer>
#include <iostream>

using namespace std;

void getCallback(redisAsyncContext *, void *r, void *privdata) {
  redisReply *reply = static_cast<redisReply *>(r);
  ExampleQt *ex = static_cast<ExampleQt *>(privdata);
  if (reply == nullptr || ex == nullptr)
    return;
  cout << "key: " << reply->str << endl;
  ex->finish();
}

void ExampleQt::run() {
  if ((m_ctx = redisAsyncConnect("localhost", 6379))->err) {
    cerr << "Error: " << m_ctx->errstr << endl;
    redisAsyncFree(m_ctx);
    emit finished();
  }
  m_adapter.setContext(m_ctx);
  redisAsyncCommand(m_ctx, NULL, NULL, "SET key %s", m_value);
  redisAsyncCommand(m_ctx, getCallback, this, "GET key");
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  ExampleQt example(argv[argc - 1]);
  QObject::connect(&example, SIGNAL(finished()), &app, SLOT(quit()));
  QTimer::singleShot(0, &example, SLOT(run()));
  return app.exec();
}
