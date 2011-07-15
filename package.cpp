/******************************************************************************
*   Copyright 2007 by Aaron Seigo <aseigo@kde.org>                            *
*   Copyright 2010 by Marco Martin <notmart@gmail.com>                        *
*   Copyright 2010 by Kevin Ottens <ervin@kde.org>                            *
*   Copyright 2009 by Rob Scheepmaker                                         *
*                                                                             *
*   This library is free software; you can redistribute it and/or             *
*   modify it under the terms of the GNU Library General Public               *
*   License as published by the Free Software Foundation; either              *
*   version 2 of the License, or (at your option) any later version.          *
*                                                                             *
*   This library is distributed in the hope that it will be useful,           *
*   but WITHOUT ANY WARRANTY; without even the implied warranty of            *
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU          *
*   Library General Public License for more details.                          *
*                                                                             *
*   You should have received a copy of the GNU Library General Public License *
*   along with this library; see the file COPYING.LIB.  If not, write to      *
*   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,      *
*   Boston, MA 02110-1301, USA.                                               *
*******************************************************************************/

#include "package.h"

#include <QDir>
#include <QFile>
#include <QRegExp>
#include <QtNetwork/QHostInfo>

#include <karchive.h>
#include <kdebug.h>
#include <kdesktopfile.h>
#include <kmimetype.h>
#include <kservicetypetrader.h>
#include <kstandarddirs.h>
#include <ktar.h>
#include <ktempdir.h>
#include <ktemporaryfile.h>
#include <kzip.h>

#ifndef PLASMA_NO_KIO
#include <kio/copyjob.h>
#include <kio/deletejob.h>
#include <kio/jobclasses.h>
#include <kio/job.h>
#endif

#include "config-plasma.h"
#include "private/package_p.h"
#include "private/packages_p.h"
#include "remote/authorizationmanager.h"
#include "remote/authorizationmanager_p.h"

namespace Plasma
{

#ifdef PLASMA_NO_KIO // Provide some convenience for dealing with folders

bool copyFolder(QString sourcePath, QString targetPath)
{
    QDir source(sourcePath);
    if(!source.exists())
        return false;

    QDir target(targetPath);
    if(!target.exists()) {
        QString targetName = target.dirName();
        target.cdUp();
        target.mkdir(targetName);
        target = QDir(targetPath);
    }

    foreach (const QString &fileName, source.entryList(QDir::Files)) {
        QString sourceFilePath = sourcePath + QDir::separator() + fileName;
        QString targetFilePath = targetPath + QDir::separator() + fileName;

        if (!QFile::copy(sourceFilePath, targetFilePath)) {
            return false;
        }
    }

    foreach (const QString &subFolderName, source.entryList(QDir::AllDirs | QDir::NoDotAndDotDot)) {
        QString sourceSubFolderPath = sourcePath + QDir::separator() + subFolderName;
        QString targetSubFolderPath = targetPath + QDir::separator() + subFolderName;

        if (!copyFolder(sourceSubFolderPath, targetSubFolderPath)) {
            return false;
        }
    }

    return true;
}

bool removeFolder(QString folderPath)
{
    QDir folder(folderPath);
    if(!folder.exists())
        return false;

    foreach (const QString &fileName, folder.entryList(QDir::Files)) {
        if (!QFile::remove(folderPath + QDir::separator() + fileName)) {
            return false;
        }
    }

    foreach (const QString &subFolderName, folder.entryList(QDir::AllDirs | QDir::NoDotAndDotDot)) {
        if (!removeFolder(folderPath + QDir::separator() + subFolderName)) {
            return false;
        }
    }

    QString folderName = folder.dirName();
    folder.cdUp();
    return folder.rmdir(folderName);
}

#endif // PLASMA_NO_KIO

Package Package::load(const QString &packageFormat, const QString &specialization)
{
    if (packageFormat.isEmpty()) {
        return Package();
    }

    if (!specialization.isEmpty()) {
        QRegExp re("[^a-zA-Z0-9\\-_]");
        // check that the provided strings are safe to use in a ServiceType query
        if (re.indexIn(specialization) == -1 && re.indexIn(packageFormat) == -1) {
            // FIXME: The query below is rather spepcific to script engines. generify if possible
            const QString component = packageFormat.right(packageFormat.size() - packageFormat.lastIndexOf('/') - 1);
            const QString constraint = QString("[X-Plasma-API] == '%1' and " "'%2' in [X-Plasma-ComponentTypes]").arg(specialization, component);
            KService::List offers = KServiceTypeTrader::self()->query("Plasma/ScriptEngine", constraint);

            if (!offers.isEmpty()) {
                KService::Ptr offer = offers.first();
                QString packageFormat = offer->property("X-Plasma-PackageFormat").toString();
                if (!packageFormat.isEmpty()) {
                    return load(packageFormat);
                }
            }
        }
    }

    if (packageFormat.startsWith("Plasma")) {
        if (packageFormat.endsWith("/Applet")) {
            return PlasmoidPackage();
        } else if (packageFormat.endsWith("/DataEngine")) {
            return DataEnginePackage();
        } else if (packageFormat.endsWith("/Runner")) {
            return RunnerPackage();
        } else if (packageFormat.endsWith("/Wallpaper")) {
            return WallpaperPackage();
        } else if (packageFormat.endsWith("/Theme")) {
            return ThemePackage();
        } else if (packageFormat.endsWith("/ContainmentActions")) {
            return ContainmentActionsPackage();
        } else if (packageFormat.endsWith("/Generic")) {
            return GenericPackage();
        }
    }

    // first we check for plugins in sycoca
    QString constraint = QString("[X-KDE-PluginInfo-Name] == '%1'").arg(packageFormat);
    KService::List offers = KServiceTypeTrader::self()->query("Plasma/Package", constraint);

    QVariantList args;
    QString error;
    foreach (const KService::Ptr &offer, offers) {
        PackageFactory *factory = (offer->createInstance<Plasma::PackageFactory>(0, args, &error));

        if (factory) {
            Package package = factory->package();
            delete factory;
            return package;
        }

        kDebug() << "Couldn't load Package for" << packageFormat
                 << "! reason given: " << error;
    }

    // if that didn't give us any love, then we try to load from a config file
    Package package;
    QString configPath("plasma/packageformats/%1rc");
    configPath = KStandardDirs::locate("data", configPath.arg(packageFormat));

    if (!configPath.isEmpty()) {
        KConfig config(configPath);
        package.read(&config);
        return package;
    }

    // try to load from absolute file path
    KUrl url(packageFormat);
    if (url.isLocalFile()) {
        KConfig config(url.toLocalFile(), KConfig::SimpleConfig);
        package.read(&config);
    }
#ifndef PLASMA_NO_KIO
    else {
        KTemporaryFile tmp;
        if (tmp.open()) {
            KIO::Job *job = KIO::file_copy(url, KUrl(tmp.fileName()),
                                           -1, KIO::Overwrite | KIO::HideProgressInfo);
            if (job->exec()) {
                KConfig config(tmp.fileName(), KConfig::SimpleConfig);
                package.read(&config);
            }
        }
    }
#endif

    return package;
}


Package::Package()
    : d(new PackagePrivate())
{
}

Package::Package(const Package &other)
    : d(new PackagePrivate(*other.d))
{
}

Package::~Package()
{
    delete d;
}

Package &Package::operator=(const Package &rhs)
{
    if (&rhs != this) {
        *d = *rhs.d;
    }

    return *this;
}

bool Package::isValid() const
{
    if (!d->valid) {
        return false;
    }

    //search for the file in all prefixes and in all possible paths for each prefix
    //even if it's a big nested loop, usually there is one prefix and one location
    //so shouldn't cause too much disk access
    QMapIterator<QByteArray, ContentStructure> it(d->contents);
    while (it.hasNext()) {
        it.next();
        if (!it.value().required) {
            continue;
        }

        bool failed = true;
        foreach (const QString &path, it.value().paths) {
            foreach (const QString &prefix, d->contentsPrefixPaths) {
                if (QFile::exists(d->path + prefix + path)) {
                    failed = false;
                    break;
                }
            }
            if (!failed) {
                break;
            }
        }

        if (failed) {
            kWarning() << "Could not find required" << (it.value().directory ? "directory" : "file") << it.key();
            d->valid = false;
            return false;
        }
    }

    return true;
}

QString Package::name(const char *key) const
{
    QMap<QByteArray, ContentStructure>::const_iterator it = d->contents.constFind(key);
    if (it == d->contents.constEnd()) {
        return QString();
    }

    return it.value().name;
}

bool Package::isRequired(const char *key) const
{
    QMap<QByteArray, ContentStructure>::const_iterator it = d->contents.constFind(key);
    if (it == d->contents.constEnd()) {
        return false;
    }

    return it.value().required;
}

QStringList Package::mimeTypes(const char *key) const
{
    QMap<QByteArray, ContentStructure>::const_iterator it = d->contents.constFind(key);
    if (it == d->contents.constEnd()) {
        return QStringList();
    }

    if (it.value().mimeTypes.isEmpty()) {
        return d->mimeTypes;
    }

    return it.value().mimeTypes;
}

QString Package::defaultPackageRoot() const
{
    return d->defaultPackageRoot;
}

void Package::setDefaultPackageRoot(const QString &packageRoot)
{
    d->defaultPackageRoot = packageRoot;
    if (!d->defaultPackageRoot.isEmpty() && !d->defaultPackageRoot.endsWith('/')) {
        d->defaultPackageRoot.append('/');
    }
}

QString Package::servicePrefix() const
{
    return d->servicePrefix;
}

void Package::setServicePrefix(const QString &servicePrefix)
{
    d->servicePrefix = servicePrefix;
}

bool Package::allowExternalPaths() const
{
    return d->externalPaths;
}

void Package::setAllowExternalPaths(bool allow)
{
    d->externalPaths = allow;
}

KPluginInfo Package::metadata() const
{
    if (!d->metadata && !d->path.isEmpty()) {
        QFileInfo fileInfo(d->path);

        if (fileInfo.isDir()) {
            d->createPackageMetadata(d->path);
        } else if (fileInfo.exists()) {
            KArchive *archive = 0;
            KMimeType::Ptr mimeType = KMimeType::findByPath(d->path);

            if (mimeType->is("application/zip")) {
                archive = new KZip(d->path);
            } else if (mimeType->is("application/x-compressed-tar") ||
                       mimeType->is("application/x-tar")|| mimeType->is("application/x-bzip-compressed-tar")) {
                archive = new KTar(d->path);
            } else {
                kWarning() << "Could not open package file, unsupported archive format:" << d->path << mimeType->name();
            }

            if (archive && archive->open(QIODevice::ReadOnly)) {
                const KArchiveDirectory *source = archive->directory();
                KTempDir tempdir;
                source->copyTo(tempdir.name());
                d->createPackageMetadata(tempdir.name());
            } else {
                kWarning() << "Could not open package file:" << d->path;
            }

            delete archive;
        }
    }

    if (!d->metadata) {
        d->metadata = new KPluginInfo();
    }

    return *d->metadata;
}

QString Package::filePath(const char *fileType, const QString &filename) const
{
    if (!d->valid) {
        //kDebug() << "package is not valid";
        return QString();
    }

    QStringList paths;

    if (qstrlen(fileType) != 0) {
        if (!d->contents.contains(fileType)) {
            //kDebug() << "package does not contain" << fileType << filename;
            return QString();
        }

        paths = d->contents[fileType].paths;

        if (paths.isEmpty()) {
            //kDebug() << "no matching path came of it, while looking for" << fileType << filename;
            return QString();
        }
    } else {
        //when filetype is empty paths is always empty, so try with an empty string
        paths << QString();
    }

    //Nested loop, but in the medium case resolves to just one iteration
    //kDebug() << "prefixes:" << prefixes.count() << prefixes;
    foreach (const QString &contentsPrefix, d->contentsPrefixPaths) {
        const QString prefix(d->path + contentsPrefix);

        foreach (const QString &path, paths) {
            QString file = prefix + path;

            if (!filename.isEmpty()) {
                file.append("/").append(filename);
            }

            //kDebug() << "testing" << file << QFile::exists("/bin/ls") << QFile::exists(file);
            if (QFile::exists(file)) {
                if (d->externalPaths) {
                    //kDebug() << "found" << file;
                    return file;
                }

                // ensure that we don't return files outside of our base path
                // due to symlink or ../ games
                QDir dir(file);
                QString canonicalized = dir.canonicalPath() + QDir::separator();

                //kDebug() << "testing that" << canonicalized << "is in" << d->path;
                if (canonicalized.startsWith(d->path)) {
                    //kDebug() << "found" << file;
                    return file;
                }
            }
        }
    }

    //kDebug() << fileType << filename << "does not exist in" << prefixes << "at root" << d->path;
    return QString();
}

QStringList Package::entryList(const char *key) const
{
    if (!d->valid) {
        return QStringList();
    }

    QMap<QByteArray, ContentStructure>::const_iterator it = d->contents.constFind(key);
    if (it == d->contents.constEnd()) {
        return QStringList();
    }

    QStringList list;
    foreach (const QString &prefix, d->contentsPrefixPaths) {
        foreach (const QString &path, it.value().paths) {
            if (it.value().directory) {
                QDir dir(d->path + prefix + path);

                if (d->externalPaths) {
                    list += dir.entryList(QDir::Files | QDir::Readable);
                } else {
                    // ensure that we don't return files outside of our base path
                    // due to symlink or ../ games
                    QString canonicalized = dir.canonicalPath();
                    if (canonicalized.startsWith(path)) {
                        list += dir.entryList(QDir::Files | QDir::Readable);
                    }
                }
            } else {
                const QString fullPath = d->path + prefix + path;
                if (!QFile::exists(fullPath)) {
                    continue;
                }

                if (d->externalPaths) {
                    list += fullPath;
                } else {
                    QDir dir(fullPath);
                    QString canonicalized = dir.canonicalPath() + QDir::separator();

                    //kDebug() << "testing that" << canonicalized << "is in" << d->path;
                    if (canonicalized.startsWith(d->path)) {
                        list += fullPath;
                    }
                }
            }
        }
    }

    return list;
}

void Package::setPath(const QString &path)
{
    if (path.isEmpty()) {
        d->path.clear();
        d->valid = false;
        return;
    }

    QDir dir(path);
    if (dir.isRelative()) {
        QString location;
        if (!d->defaultPackageRoot.isEmpty()) {
            dir.setPath(d->defaultPackageRoot);
            if (dir.isRelative()) {
                location = KStandardDirs::locate("data", d->defaultPackageRoot + path);
            } else {
                location = d->defaultPackageRoot + path;
            }
        }

        if (location.isEmpty()) {
            location = KStandardDirs::locate("data", path);

            if (location.isEmpty()) {
                d->path.clear();
                d->valid = false;
                return;
            }
        }

        dir.setPath(location);
    }

    QString basePath = dir.canonicalPath();
    bool valid = QFile::exists(basePath);

    if (valid) {
        QFileInfo info(basePath);
        if (info.isDir() && !basePath.endsWith('/')) {
            basePath.append('/');
        }
        //kDebug() << "basePath is" << basePath;
    } else {
        kDebug() << path << "invalid, basePath is" << basePath;
        return;
    }

    if (d->path == basePath) {
        return;
    }

    d->path = basePath;
    delete d->metadata;
    d->metadata = 0;
    pathChanged();
    d->valid = !d->path.isEmpty();
}

const QString Package::path() const
{
    return d->path;
}

void Package::pathChanged()
{
    // default impl does nothing, this is a hook for subclasses.
}

QStringList Package::contentsPrefixPaths() const
{
    return d->contentsPrefixPaths;
}

void Package::setContentsPrefixPaths(const QStringList &prefixPaths)
{
    d->contentsPrefixPaths = prefixPaths;
    if (d->contentsPrefixPaths.isEmpty()) {
        d->contentsPrefixPaths << QString();
    }
}

QString Package::contentsHash() const
{
    if (!d->valid) {
        kWarning() << "can not create hash due to Package being invalid";
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha1);
    QString metadataPath = d->path + "metadata.desktop";
    if (QFile::exists(metadataPath)) {
        QFile f(metadataPath);
        if (f.open(QIODevice::ReadOnly)) {
            while (!f.atEnd()) {
                hash.addData(f.read(1024));
            }
        } else {
            kWarning() << "could not add" << f.fileName() << "to the hash; file could not be opened for reading.";
        }
    } else {
        kWarning() << "no metadata at" << metadataPath;
    }

    foreach (QString prefix, d->contentsPrefixPaths) {
        const QString basePath = d->path + prefix;
        QDir dir(basePath);

        if (!dir.exists()) {
            return QString();
        }

        d->updateHash(basePath, QString(), dir, hash);
    }

    return hash.result();
}

void Package::addDirectoryDefinition(const char *key, const QString &path, const QString &name)
{
    ContentStructure s;

    if (d->contents.contains(key)) {
        s = d->contents[key];
    }

    if (!name.isEmpty()) {
        s.name = name;
    }

    s.paths.append(path);
    s.directory = true;

    d->contents[key] = s;
}

void Package::addFileDefinition(const char *key, const QString &path, const QString &name)
{
    ContentStructure s;

    if (d->contents.contains(key)) {
        s = d->contents[key];
    }

    if (!name.isEmpty()) {
        s.name = name;
    }

    s.paths.append(path);
    s.directory = false;

    d->contents[key] = s;
}

void Package::removeDefinition(const char *key)
{
    d->contents.remove(key);
}

void Package::setRequired(const char *key, bool required)
{
    QMap<QByteArray, ContentStructure>::iterator it = d->contents.find(key);
    if (it == d->contents.end()) {
        return;
    }

    it.value().required = required;
}

void Package::setDefaultMimeTypes(QStringList mimeTypes)
{
    d->mimeTypes = mimeTypes;
}

void Package::setMimeTypes(const char *key, QStringList mimeTypes)
{
    QMap<QByteArray, ContentStructure>::iterator it = d->contents.find(key);
    if (it == d->contents.end()) {
        return;
    }

    it.value().mimeTypes = mimeTypes;
}

QList<const char*> Package::directories() const
{
    QList<const char*> dirs;
    QMap<QByteArray, ContentStructure>::const_iterator it = d->contents.constBegin();
    while (it != d->contents.constEnd()) {
        if (it.value().directory) {
            dirs << it.key();
        }
        ++it;
    }
    return dirs;
}

QList<const char*> Package::requiredDirectories() const
{
    QList<const char*> dirs;
    QMap<QByteArray, ContentStructure>::const_iterator it = d->contents.constBegin();
    while (it != d->contents.constEnd()) {
        if (it.value().directory &&
            it.value().required) {
            dirs << it.key();
        }
        ++it;
    }
    return dirs;
}

QList<const char*> Package::files() const
{
    QList<const char*> files;
    QMap<QByteArray, ContentStructure>::const_iterator it = d->contents.constBegin();
    while (it != d->contents.constEnd()) {
        if (!it.value().directory) {
            files << it.key();
        }
        ++it;
    }
    return files;
}

QList<const char*> Package::requiredFiles() const
{
    QList<const char*> files;
    QMap<QByteArray, ContentStructure>::const_iterator it = d->contents.constBegin();
    while (it != d->contents.constEnd()) {
        if (!it.value().directory && it.value().required) {
            files << it.key();
        }
        ++it;
    }

    return files;
}

void Package::read(const KConfigBase *config)
{
    d->contents.clear();
    d->mimeTypes.clear();
    KConfigGroup general(config, QString());
    d->type = general.readEntry("Type", QString());
    d->contentsPrefixPaths = general.readEntry("ContentsPrefixPaths", d->contentsPrefixPaths);
    d->defaultPackageRoot = general.readEntry("DefaultPackageRoot", d->defaultPackageRoot);
    d->externalPaths = general.readEntry("AllowExternalPaths", d->externalPaths);

    QStringList groups = config->groupList();
    foreach (const QString &group, groups) {
        KConfigGroup entry(config, group);
        QByteArray key = group.toAscii();

        QString path = entry.readEntry("Path", QString());
        QString name = entry.readEntry("Name", QString());
        QStringList mimeTypes = entry.readEntry("Mimetypes", QStringList());
        bool directory = entry.readEntry("Directory", false);
        bool required = entry.readEntry("Required", false);

        if (directory) {
            addDirectoryDefinition(key, path, name);
        } else {
            addFileDefinition(key, path, name);
        }

        setMimeTypes(key, mimeTypes);
        setRequired(key, required);
    }
}

void Package::write(KConfigBase *config) const
{
    KConfigGroup general = KConfigGroup(config, "");
    general.writeEntry("ContentsPrefixPaths", d->contentsPrefixPaths);
    general.writeEntry("DefaultPackageRoot", d->defaultPackageRoot);
    general.writeEntry("AllowExternalPaths", d->externalPaths);

    QMap<QByteArray, ContentStructure>::const_iterator it = d->contents.constBegin();
    while (it != d->contents.constEnd()) {
        KConfigGroup group = config->group(it.key());
        group.writeEntry("Path", it.value().paths);
        group.writeEntry("Name", it.value().name);
        if (!it.value().mimeTypes.isEmpty()) {
            group.writeEntry("Mimetypes", it.value().mimeTypes);
        }
        if (it.value().directory) {
            group.writeEntry("Directory", true);
        }
        if (it.value().required) {
            group.writeEntry("Required", true);
        }

        ++it;
    }
}

bool Package::installPackage(const QString &package, const QString &packageRoot)
{
    //TODO: report *what* failed if something does fail
    QDir root(packageRoot);

    if (!root.exists()) {
        KStandardDirs::makeDir(packageRoot);
        if (!root.exists()) {
            kWarning() << "Could not create package root directory:" << packageRoot;
            return false;
        }
    }

    QFileInfo fileInfo(package);
    if (!fileInfo.exists()) {
        kWarning() << "No such file:" << package;
        return false;
    }

    QString path;
    KTempDir tempdir;
    bool archivedPackage = false;

    if (fileInfo.isDir()) {
        // we have a directory, so let's just install what is in there
        path = package;

        // make sure we end in a slash!
        if (path[path.size() - 1] != '/') {
            path.append('/');
        }
    } else {
        KArchive *archive = 0;
        KMimeType::Ptr mimetype = KMimeType::findByPath(package);

        if (mimetype->is("application/zip")) {
            archive = new KZip(package);
        } else if (mimetype->is("application/x-compressed-tar") ||
                   mimetype->is("application/x-tar")|| mimetype->is("application/x-bzip-compressed-tar")) {
            archive = new KTar(package);
        } else {
            kWarning() << "Could not open package file, unsupported archive format:" << package << mimetype->name();
            return false;
        }

        if (!archive->open(QIODevice::ReadOnly)) {
            kWarning() << "Could not open package file:" << package;
        delete archive;
            return false;
        }

        archivedPackage = true;
        path = tempdir.name();

        const KArchiveDirectory *source = archive->directory();
        source->copyTo(path);

        QStringList entries = source->entries();
        if (entries.count() == 1) {
            const KArchiveEntry *entry = source->entry(entries[0]);
            if (entry->isDirectory()) {
                path.append(entry->name()).append("/");
            }
        }

        delete archive;
    }

    QString metadataPath = path + "metadata.desktop";
    if (!QFile::exists(metadataPath)) {
        kWarning() << "No metadata file in package" << package << metadataPath;
        return false;
    }

    KPluginInfo meta(metadataPath);
    QString targetName = meta.pluginName();

    if (targetName.isEmpty()) {
        kWarning() << "Package plugin name not specified";
        return false;
    }

    // Ensure that package names are safe so package uninstall can't inject
    // bad characters into the paths used for removal.
    QRegExp validatePluginName("^[\\w-\\.]+$"); // Only allow letters, numbers, underscore and period.
    if (!validatePluginName.exactMatch(targetName)) {
        kWarning() << "Package plugin name " << targetName << "contains invalid characters";
        return false;
    }

    targetName = packageRoot + '/' + targetName;
    if (QFile::exists(targetName)) {
        kWarning() << targetName << "already exists";
        return false;
    }

    if (archivedPackage) {
        // it's in a temp dir, so just move it over.
#ifndef PLASMA_NO_KIO
        KIO::CopyJob *job = KIO::move(KUrl(path), KUrl(targetName), KIO::HideProgressInfo);
        const bool ok = job->exec();
        const QString errorString = job->errorString();
#else
        const bool ok = copyFolder(path, targetName);
        removeFolder(path);
        const QString errorString("unknown");
#endif
        if (!ok) {
            kWarning() << "Could not move package to destination:" << targetName << " : " << errorString;
            return false;
        }
    } else {
        // it's a directory containing the stuff, so copy the contents rather
        // than move them
#ifndef PLASMA_NO_KIO
        KIO::CopyJob *job = KIO::copy(KUrl(path), KUrl(targetName), KIO::HideProgressInfo);
        const bool ok = job->exec();
        const QString errorString = job->errorString();
#else
        const bool ok = copyFolder(path, targetName);
        const QString errorString("unknown");
#endif
        if (!ok) {
            kWarning() << "Could not copy package to destination:" << targetName << " : " << errorString;
            return false;
        }
    }

    if (archivedPackage) {
        // no need to remove the temp dir (which has been successfully moved if it's an archive)
        tempdir.setAutoRemove(false);
    }

    if (!d->servicePrefix.isEmpty()) {
        // and now we register it as a service =)
        QString metaPath = targetName + "/metadata.desktop";
        KDesktopFile df(metaPath);
        KConfigGroup cg = df.desktopGroup();

        // Q: should not installing it as a service disqualify it?
        // Q: i don't think so since KServiceTypeTrader may not be
        // used by the installing app in any case, and the
        // package is properly installed - aseigo

        //TODO: reduce code duplication with registerPackage below

        QString serviceName = d->servicePrefix + meta.pluginName();

        QString service = KStandardDirs::locateLocal("services", serviceName + ".desktop");
#ifndef PLASMA_NO_KIO
        KIO::FileCopyJob *job = KIO::file_copy(metaPath, service, -1, KIO::HideProgressInfo);
        const bool ok = job->exec();
        const QString errorString = job->errorString();
#else
        const bool ok = QFile::copy(metaPath, service);
        const QString errorString("unknown");
#endif
        if (ok) {
            // the icon in the installed file needs to point to the icon in the
            // installation dir!
            QString iconPath = targetName + '/' + cg.readEntry("Icon");
            QFile icon(iconPath);
            if (icon.exists()) {
                KDesktopFile df(service);
                KConfigGroup cg = df.desktopGroup();
                cg.writeEntry("Icon", iconPath);
            }
        } else {
            kWarning() << "Could not register package as service (this is not necessarily fatal):" << serviceName << " : " << errorString;
        }
    }

    return true;
}

bool Package::uninstallPackage(const QString &packageName, const QString &packageRoot)
{
    // We need to remove the package directory and its metadata file.
    const QString targetName = packageRoot + '/' + packageName;

    if (!QFile::exists(targetName)) {
        kWarning() << targetName << "does not exist";
        return false;
    }

    QString serviceName = d->servicePrefix + packageName;

    QString service = KStandardDirs::locateLocal("services", serviceName + ".desktop");
    kDebug() << "Removing service file " << service;
    bool ok = QFile::remove(service);

    if (!ok) {
        kWarning() << "Unable to remove " << service;
    }

#ifndef PLASMA_NO_KIO
    KIO::DeleteJob *job = KIO::del(KUrl(targetName));
    ok = job->exec();
    const QString errorString = job->errorString();
#else
    ok = removeFolder(targetName);
    const QString errorString("unknown");
#endif
    if (!ok) {
        kWarning() << "Could not delete package from:" << targetName << " : " << errorString;
        return false;
    }

    return true;
}

PackagePrivate::PackagePrivate()
        : servicePrefix("plasma-applet-"),
          metadata(0),
          externalPaths(false),
          valid(false)
{
    contentsPrefixPaths << "contents/";
}

PackagePrivate::PackagePrivate(const PackagePrivate &other)
{
    *this = other;
}

PackagePrivate::~PackagePrivate()
{
    delete metadata;
}

PackagePrivate &PackagePrivate::operator=(const PackagePrivate &rhs)
{
    path = rhs.path;
    contentsPrefixPaths = rhs.contentsPrefixPaths;
    servicePrefix = rhs.servicePrefix;
    contents = rhs.contents;
    mimeTypes = rhs.mimeTypes;
    defaultPackageRoot = rhs.defaultPackageRoot;
    servicePrefix = rhs.servicePrefix;
    metadata = 0;
    externalPaths = rhs.externalPaths;
    valid = rhs.valid;
    return *this;
}

void PackagePrivate::updateHash(const QString &basePath, const QString &subPath, const QDir &dir, QCryptographicHash &hash)
{
    // hash is calculated as a function of:
    // * files ordered alphabetically by name, with each file's:
    //      * path relative to the content root
    //      * file data
    // * directories ordered alphabetically by name, with each dir's:
    //      * path relative to the content root
    //      * file listing (recursing)
    // symlinks (in both the file and dir case) are handled by adding
    // the name of the symlink itself and the abs path of what it points to

    const QDir::SortFlags sorting = QDir::Name | QDir::IgnoreCase;
    const QDir::Filters filters = QDir::Hidden | QDir::System | QDir::NoDotAndDotDot;
    foreach (const QString &file, dir.entryList(QDir::Files | filters, sorting)) {
        if (!subPath.isEmpty()) {
            hash.addData(subPath.toUtf8());
        }

        hash.addData(file.toUtf8());

        QFileInfo info(dir.path() + '/' + file);
        if (info.isSymLink()) {
            hash.addData(info.symLinkTarget().toUtf8());
        } else {
            QFile f(info.filePath());
            if (f.open(QIODevice::ReadOnly)) {
                while (!f.atEnd()) {
                    hash.addData(f.read(1024));
                }
            } else {
                kWarning() << "could not add" << f.fileName() << "to the hash; file could not be opened for reading. "
                           << "permissions fail?" << info.permissions() << info.isFile();
            }
        }
    }

    foreach (const QString &subDirPath, dir.entryList(QDir::Dirs | filters, sorting)) {
        const QString relativePath = subPath + subDirPath + '/';
        hash.addData(relativePath.toUtf8());

        QDir subDir(dir.path());
        subDir.cd(subDirPath);

        if (subDir.path() != subDir.canonicalPath()) {
            hash.addData(subDir.canonicalPath().toUtf8());
        } else {
            updateHash(basePath, relativePath, subDir, hash);
        }
    }
}

void PackagePrivate::createPackageMetadata(const QString &path)
{
    delete metadata;

    QString metadataPath(path + "/metadata.desktop");
    if (!QFile::exists(metadataPath)) {
        kWarning() << "No metadata file in the package, expected it at:" << metadataPath;
        metadataPath.clear();
    }

    metadata = new KPluginInfo(metadataPath);
}

PackageFactory::PackageFactory(QObject *parent, const QVariantList &args)
    : QObject(parent)
{
    Q_UNUSED(args)
}

Package PackageFactory::package() const
{
    return Package();
}

} // Namespace
