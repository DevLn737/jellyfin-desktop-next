#pragma once

#include <jfd/api/Result.h>
#include <jfd/library/ItemListModel.h>

#include <QObject>
#include <QString>
#include <QTimer>

#include <optional>

namespace jfd::api {
  class JellyfinClient;
}

namespace jfd::library {

  class LibraryUpdates;

  // Search as you type: the query goes out once typing pauses, and only the
  // answer to the latest text is shown, however the answers arrive.
  //
  // Titles (films, series, episodes, videos), music (albums, artists,
  // playlists, tracks) and people (actors, directors) are searched; people
  // come from a request of their own and are listed after the rest. A
  // failed people request leaves them out without an error.
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
      int peopleLimit = 12;
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
    // Shows the results once both requests answered.
    void finish();

    api::JellyfinClient& m_client;
    Options m_options;
    QString m_text;
    QString m_sent;
    ItemListModel m_results;
    QTimer m_delay;
    quint64 m_serial = 0;
    // The answers to the latest request so far.
    std::optional<api::Result<api::ItemPage>> m_items;
    std::optional<QList<api::Item>> m_people;
    bool m_loading = false;
    QString m_errorText;
  };

} // namespace jfd::library
