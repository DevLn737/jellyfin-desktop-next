#pragma once

#include <jfd/library/ItemListModel.h>

#include <QObject>
#include <QString>
#include <QTimer>

namespace jfd::api {
  class JellyfinClient;
}

namespace jfd::library {

  class LibraryUpdates;

  // Search as you type: the query goes out once typing pauses, and only the
  // answer to the latest text is shown, however the answers arrive.
  class SearchModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(jfd::library::ItemListModel* results READ results CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)

  public:
    struct Options {
      int delayMs = 300;
      int limit = 60;
      int cardWidth = 200;
      qreal devicePixelRatio = 1.0;
    };

    SearchModel(api::JellyfinClient& client, Options options, QObject* parent = nullptr);
    explicit SearchModel(api::JellyfinClient& client, QObject* parent = nullptr)
        : SearchModel(client, Options{}, parent) {}

    QString text() const { return m_text; }
    void setText(const QString& text);
    ItemListModel* results() { return &m_results; }
    bool loading() const { return m_loading; }
    QString errorText() const { return m_errorText; }

    void follow(LibraryUpdates& updates);

    // Sends the current text now instead of after the delay (Enter).
    Q_INVOKABLE void submit();

  Q_SIGNALS:
    void textChanged();
    void loadingChanged();
    void errorTextChanged();

  private:
    void setLoading(bool loading);
    void setError(const QString& text);

    api::JellyfinClient& m_client;
    Options m_options;
    QString m_text;
    QString m_sent;
    ItemListModel m_results;
    QTimer m_delay;
    quint64 m_serial = 0;
    bool m_loading = false;
    QString m_errorText;
  };

} // namespace jfd::library
