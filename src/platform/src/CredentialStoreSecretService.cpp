#include <jfd/platform/CredentialStore.h>

#include <libsecret/secret.h>

namespace jfd::platform {

  namespace {
    const SecretSchema* schema() {
      static const SecretSchema value = {
          "org.jellyfin.DesktopNext.Credential",
          SECRET_SCHEMA_NONE,
          {{"key", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}},
          0,
          nullptr,
          nullptr,
          nullptr,
          nullptr,
          nullptr,
          nullptr,
          nullptr};
      return &value;
    }

    // A missing or locked Secret Service shows up as an error on the first
    // call; it is reported as Unavailable, not as a missing secret.
    CredentialStore::Status statusFrom(GError* error) {
      if (!error) return CredentialStore::Status::Ok;
      const bool unavailable = error->domain != SECRET_ERROR;
      g_error_free(error);
      return unavailable ? CredentialStore::Status::Unavailable : CredentialStore::Status::Failed;
    }

    class SecretServiceCredentialStore final : public CredentialStore {
    public:
      bool isAvailable() const override {
        GError* error = nullptr;
        SecretService* service = secret_service_get_sync(SECRET_SERVICE_NONE, nullptr, &error);
        if (service) g_object_unref(service);
        if (error) g_error_free(error);
        return service != nullptr;
      }

      ReadResult read(const QString& key) const override {
        GError* error = nullptr;
        const QByteArray k = key.toUtf8();
        gchar* secret =
            secret_password_lookup_sync(schema(), nullptr, &error, "key", k.constData(), nullptr);
        if (error) return {statusFrom(error), {}};
        if (!secret) return {Status::NotFound, {}};
        QString value = QString::fromUtf8(secret);
        secret_password_free(secret);
        return {Status::Ok, value};
      }

      Status write(const QString& key, const QString& secret) override {
        GError* error = nullptr;
        const QByteArray k = key.toUtf8();
        const QByteArray label = (QStringLiteral("Jellyfin Desktop Next: ") + key).toUtf8();
        QByteArray value = secret.toUtf8();
        secret_password_store_sync(schema(), SECRET_COLLECTION_DEFAULT, label.constData(),
                                   value.constData(), nullptr, &error, "key", k.constData(),
                                   nullptr);
        value.fill('\0');
        return statusFrom(error);
      }

      Status remove(const QString& key) override {
        GError* error = nullptr;
        const QByteArray k = key.toUtf8();
        secret_password_clear_sync(schema(), nullptr, &error, "key", k.constData(), nullptr);
        return statusFrom(error);
      }
    };
  } // namespace

  std::unique_ptr<CredentialStore> createSystemCredentialStore() {
    return std::make_unique<SecretServiceCredentialStore>();
  }

} // namespace jfd::platform
