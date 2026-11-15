# Зачем всё это

Привет! Вторая статья на habr и третья за жизнь. Сегодня хочется в формате мини-урока поговорить про такую вещь, как `дебинизация` проекта - процесс формирования `.deb`-пакета из вашего проекта.

Хочется попробовать подать этот материал в формате урока.

Весь приведённый материал в статье представлен здесь:
```text
https://github.com/X0re4ik/dpkg-study-cpp
```
Так как данная статья позиционирует себя как туториал, то хотелось бы добавить ей немножко интерактивности. Переходите по ссылке: [https://x0re4ik.ru/study/tutorials/001-cpp-study](https://x0re4ik.ru/study/tutorials/001-cpp-study) — заботливый GLM сформировал интерактивный учебник по статье (буду очень благодарен за обратную связь: насколько идея живая).

# Чему научимся

По ходу статьи пройдём путь от ручной сборки до автоматизации:

1. Соберём минимально жизнеспособный пакет **руками**, без специализированных утилит (`dpkg --build`).
2. Добавим бинарник, конфиг и systemd-юнит с postinst/prerm-скриптами.
3. Поймём, **почему пакет не обновляется** при повторном `apt-get install` и как работает версионирование.
4. Разберёмся с **зависимостями**: как выяснить, от чего зависит бинарник, и что писать в `Depends:`.
5. Заглянем **внутрь .deb** и увидим, что никакой магии там нет.
6. Автоматизируем всё то же самое через `dpkg-buildpackage` и `debhelper` - и поймём, что утилиты просто делают за нас то, что мы уже умеем делать руками.

# Дано

Проект `pg-server` - небольшой HTTP-сервер на C++, который:

- собирается обычным `make` в один бинарник;
- читает из YAML-конфига порт, на котором ему нужно подняться;
- на любой HTTP-запрос отвечает версией установленного драйвера PostgreSQL (`libpq`).

Структура нашего приложения на данный момент следующая:
```bash
.
├── Makefile
├── Dockerfile
├── example.config.yaml          # конфигурационный файл
├── pg-server.service            # юнит systemd
└── src
    └── main.cpp
```

`example.config.yaml`:
```yaml
port: 3031
```

`pg-server.service`:
```ini
[Unit]
Description=PostgreSQL version HTTP server

[Service]
Type=simple
ExecStart=/usr/bin/pg-server --config /etc/pg-server/config.yaml

DynamicUser=yes
NoNewPrivileges=yes
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
```

# Чего хотим

Мы хотим, чтобы наше приложение ставилось одной командой:

```bash
apt-get install ./pg-server.deb
```
А удаляться наше приложение должно так же легко, как и ставиться:

```bash
apt-get remove pg-server
```

# Ручная сборка проекта

Начнём с ручного формирования deb-пакета. Первым делом обсудим структуру проекта, она будет следующей:

```bash
├── Makefile
├── packages
│   ├── pg-server-amd64-dpkg
│   │   ├── DEBIAN
│   │   │   ├── control                         # описание пакета: имя, версия, архитектура
│   │   │   ├── postinst                        # скрипт, выполняемый после установки
│   │   │   └── prerm                           # скрипт, выполняемый перед удалением
│   │   ├── etc
│   │   │   └── pg-server
│   │   │       └── config.yaml                 # конфигурационный файл проекта
│   │   ├── lib
│   │   │   └── systemd
│   │   │       └── system
│   │   │           └── pg-server.service       # юнит systemd
│   │   └── usr
│   │       └── bin
│   │           └── pg-server               # наш бинарник
│   └── pg-server-amd64-dpkg.deb
└── src
    └── main.cpp
```

При первом взгляде на целевую картинку пакета можно заметить, что она повторяет то, как файлы будут располагаться на целевой машине: `pg-server` после установки попадёт в `/usr/bin/pg-server`, `config.yaml` - в `/etc/pg-server/config.yaml`, а юнит - в `/lib/systemd/system/pg-server.service`. Один из фокусов deb-пакетов раскрыт: куда встанет тот или иной файл, определяется при создании самого пакета.

Для начала соберём каркас пакета - папку с манифестом и пока пустыми скриптами:
```bash
# Создаём каркас пакета
mkdir -p packages/pg-server-amd64-dpkg/
mkdir -p packages/pg-server-amd64-dpkg/DEBIAN/

# Скрипты пока оставим пустыми - их содержимым займёмся ниже
touch packages/pg-server-amd64-dpkg/DEBIAN/control
touch packages/pg-server-amd64-dpkg/DEBIAN/postinst
touch packages/pg-server-amd64-dpkg/DEBIAN/prerm
```

`packages/pg-server-amd64-dpkg/DEBIAN/control` - здесь хранится манифест нашего проекта:

```text
Package: pg-server
Section: utils
Priority: optional
Version: 0.0.1
Maintainer: Mochalov Anton <xoore4ik@gmail.com>
Standards-Version: 3.9.4
Architecture: amd64
Description: Учимся собирать свой простой deb-пакет
```

Мы задаём название проекта, владельца пакета, архитектуру и описание. Разбор `postinst`, `prerm` и юнита systemd оставим на потом. Как только файл `control` готов, мы уже можем собрать наш deb-пакет - хоть изначально он и будет бесполезен:

```bash
dpkg --build ./packages/pg-server-amd64-dpkg
sudo apt-get install ./packages/pg-server-amd64-dpkg.deb
```

После установки пакета Linux хранит связь между пакетом и связанными с ним файлами, посмотреть её можно по пути `/var/lib/dpkg/info/pg-server.list`.

В нашем простом случае этот файл будет иметь следующий вид:
```bash
cat /var/lib/dpkg/info/pg-server.list

/.
```

Единственная запись `/.` - это корень пакета: файлов в нём пока нет, только манифест.

## Наполняем пакет

Теперь положим в пакет бинарник и конфигурацию. Сначала соберём сам сервер:

```bash
# Собираем бинарник
make

# Кладём исполняемый файл в пакет
mkdir -p packages/pg-server-amd64-dpkg/usr/bin/
cp pg-server packages/pg-server-amd64-dpkg/usr/bin/pg-server

# Кладём конфигурационный файл
mkdir -p packages/pg-server-amd64-dpkg/etc/pg-server/
cp example.config.yaml packages/pg-server-amd64-dpkg/etc/pg-server/config.yaml
```

Пересобираем и переустанавливаем. Нюанс: apt не станет ставить пакет, если его версия не изменилась - почему так происходит, разберём в главе про версионирование. Пока просто удалим и поставим заново:

```bash
sudo apt-get remove pg-server

dpkg --build ./packages/pg-server-amd64-dpkg
sudo apt-get install ./packages/pg-server-amd64-dpkg.deb
```

Проверяем, что файлы реально встали на свои места:

```bash
cat /etc/pg-server/config.yaml
port: 3031

ls /usr/bin/pg-server
/usr/bin/pg-server
```

## Подключаем systemd

Сервер ставится и работает, но у нас же сервер - хочется, чтобы он управлялся через `systemctl`. Юнит `pg-server.service` у нас уже есть - мы показали его в «Дано». Кладём его туда, где systemd ищет юниты, - по пути из дерева выше:

```bash
# Кладём юнит в пакет
mkdir -p packages/pg-server-amd64-dpkg/lib/systemd/system/
cp pg-server.service packages/pg-server-amd64-dpkg/lib/systemd/system/pg-server.service
```

Пересобирать пакет уже сейчас не будем - сделаем это вместе со скриптами в следующем разделе.

## Автозапуск: maintainer-скрипты

Ранее мы упоминали, что наш сервер должен автоматически запускаться сразу же после установки и корректно останавливаться после удаления пакета - не зря же мы положили в пакет systemd-юнит.

Здесь нам помогут maintainer-скрипты: `preinst` (pre-install), `postinst` (post-install), `prerm` (pre-remove) и `postrm` (post-remove). Названия говорят сами за себя - они выполняются до и после установки/удаления соответственно. В нашем пакете мы будем использовать `postinst` и `prerm`; `preinst` и `postrm` упомянули для полноты картины.

Использовать эти файлы нужно аккуратно: установка пакета происходит от root, а значит и скрипты выполняются от root - в этот момент у нас в руках вся система.

В нашем случае мы сразу после установки запустим наш сервер (`postinst`), а при удалении выполним мягкий стоп (`prerm`):

```bash
vi ./packages/pg-server-amd64-dpkg/DEBIAN/postinst

#!/bin/sh
systemctl --system daemon-reload
systemctl enable pg-server
systemctl start pg-server
```

```bash
vi ./packages/pg-server-amd64-dpkg/DEBIAN/prerm

#!/bin/sh
systemctl stop pg-server
systemctl --system daemon-reload
```

dpkg не исправляет права сам - скрипты должны быть исполняемыми, иначе установка упадёт:

```bash
chmod 755 ./packages/pg-server-amd64-dpkg/DEBIAN/postinst ./packages/pg-server-amd64-dpkg/DEBIAN/prerm
```

Мы изменили содержимое пакета, но разбором обновления пакетов займёмся позже, поэтому, как и раньше, просто удалим пакет, пересоберём его и поставим заново:

```bash
sudo apt-get remove pg-server
dpkg --build ./packages/pg-server-amd64-dpkg
sudo apt-get install ./packages/pg-server-amd64-dpkg.deb
```

Теперь проверим наш сервер и убедимся, что он автоматически запустился сразу же после установки:
```bash
sudo systemctl status pg-server

● pg-server.service - PostgreSQL version HTTP server
     Loaded: loaded (/lib/systemd/system/pg-server.service; enabled; vendor preset: enabled)
     Active: active (running) since Sun 2026-11-15 09:37:47 MSK; 40s ago
   Main PID: 1660198 (pg-server)
      Tasks: 1 (limit: 18676)
     Memory: 1.7M
        CPU: 17ms
     CGroup: /system.slice/pg-server.service
             └─1660198 /usr/bin/pg-server --config /etc/pg-server/config.yaml

ноя 15 09:37:47 ... systemd[1]: Started PostgreSQL version HTTP server.
ноя 15 09:37:47 ... pg-server[1660198]: Listening on http://127.0.0.1:3031
```

Проверим теперь, что при удалении пакета сервер автоматически остановится. Вывод `systemctl status` здесь не поможет - юнит исчезнет вместе с пакетом, поэтому подсмотрим в журнал systemd:

```bash
sudo apt-get remove pg-server
journalctl -u pg-server --no-pager | tail -n 3

ноя 15 09:41:02 ... systemd[1]: Stopping PostgreSQL version HTTP server...
ноя 15 09:41:02 ... systemd[1]: pg-server.service: Deactivated successfully.
ноя 15 09:41:02 ... systemd[1]: Stopped PostgreSQL version HTTP server.
```

`prerm` отработал: в момент удаления сервер был корректно остановлен. А сам юнит исчез вместе с пакетом:

```bash
sudo systemctl status pg-server

Unit pg-server.service could not be found.
```

На этом ручная сборка закончена. Заодно проявилось главное свойство пакета: он - снапшот того, каким мы хотим видеть состояние файловой системы на целевой машине.


# Как происходит обновление пакета

Ранее мы отмечали: если внести изменения в пакет и отдать клиенту обновлённый .deb, у него ничего не произойдёт. Клиенту пришлось бы либо явно переустановить пакет (`apt-get install --reinstall ...`), либо сначала удалить старый (`apt-get remove pg-server`) и только потом поставить новый - это крайне неудобно. В Linux обновление пакетов работает через версии: если версия пакета не изменилась, переустановка не произойдёт, даже если содержимое пакета изменилось.

Начнём с апгрейда. Изменим версию пакета в файле `control`, например, с `0.0.1` на `0.0.2` (`Version: 0.0.2`), и попробуем поставить его поверх уже установленного:
```bash
sudo apt-get install ./packages/pg-server-amd64-dpkg.deb


Reading package lists... Done
Building dependency tree... Done
Reading state information... Done
Note, selecting 'pg-server' instead of './packages/pg-server-amd64-dpkg.deb'
The following packages were automatically installed and are no longer required:
  libc++1-14 libc++abi1-14 libunwind-14
Use 'sudo apt autoremove' to remove them.
The following packages will be upgraded:
  pg-server
1 upgraded, 0 newly installed, 0 to remove and 112 not upgraded.
Need to get 0 B/20,2 kB of archives.
After this operation, 0 B of additional disk space will be used.
Get:1 /home/xoore4ik/X0re4ik/projects/deb-cpp-study/packages/pg-server-amd64-dpkg.deb pg-server amd64 0.0.2 [20,2 kB]
(Reading database ... 418612 files and directories currently installed.)
Preparing to unpack .../pg-server-amd64-dpkg.deb ...
Unpacking pg-server (0.0.2) over (0.0.1) ...    # <-- Смотреть сюда
Setting up pg-server (0.0.2) ...
```

apt сравнил версии, понял, что новая выше, и выполнил полноценный апгрейд.

А теперь попробуем поставить тот же самый пакет - с той же версией `0.0.2` - ещё раз:

```bash
sudo apt-get install ./packages/pg-server-amd64-dpkg.deb


Reading package lists... Done
Building dependency tree... Done
Reading state information... Done
Note, selecting 'pg-server' instead of './packages/pg-server-amd64-dpkg.deb'

pg-server is already the newest version (0.0.2).    # <-- Смотреть сюда

The following packages were automatically installed and are no longer required:
  libc++1-14 libc++abi1-14 libunwind-14
Use 'sudo apt autoremove' to remove them.
0 upgraded, 0 newly installed, 0 to remove and 112 not upgraded.
```

Версия не изменилась - и dpkg даже не стал распаковывать содержимое, хотя внутри пакета мы могли что-то поменять. Именно поэтому после изменения пакета нужно поднимать его версию в `control`.

# Как вести Depends

Все это время мы не замечали слона в комнате: ещё на первом шаге мы намеренно не указали `Depends:` - список библиотек, от которых зависит наше приложение. На моей машине, как и на вашей, все необходимые библиотеки уже установлены, поэтому всё работало. Но если перенести пакет на чистую систему, получим неприятный сюрприз. Проверим в изолированной ubuntu через docker:

```bash
docker run --rm -it \
  -v "$PWD/packages:/packages:ro" \
  ubuntu:22.04 bash
```

И внутри контейнера поставим наш пакет:

```bash
apt-get update && apt-get install /packages/pg-server-amd64-dpkg.deb
```

Установка прошла успешно. Пробуем запустить сервер:

```bash
/usr/bin/pg-server --config /etc/pg-server/config.yaml
/usr/bin/pg-server: error while loading shared libraries: libpq.so.5: cannot open shared object file: No such file or directory
```

Пакет встал, но приложение не запускается. Всё потому, что наш сервер зависит от двух внешних библиотек - `libpq` и `yaml-cpp` - которые на чистой машине просто отсутствуют. Нам повезло, что на нашей машине они были.

Узнаем, от чего именно зависит наш бинарник:

```bash
objdump -p ./pg-server | grep "NEEDED"
  NEEDED               libpq.so.5
  NEEDED               libyaml-cpp.so.0.7
  NEEDED               libstdc++.so.6
  NEEDED               libgcc_s.so.1
  NEEDED               libc.so.6
```

Список `NEEDED` - это и есть содержимое `Depends:` в терминах deb-пакета. Причём `libstdc++`, `libgcc_s` и `libc` есть в любой системе - их можно не указывать. А вот `libpq.so.5` и `libyaml-cpp.so.0.7` нужно обеспечить.

Где физически лежит библиотека, подскажет `ldconfig` - он хранит кэш того, что видит линковщик:

```bash
sudo ldconfig -p | grep libpq.so.5
        libpq.so.5 (libc6,x86-64) => /lib/x86_64-linux-gnu/libpq.so.5
```

Осталось понять, какому пакету принадлежит файл - это умеет `dpkg`, он хранит список файлов каждого установленного пакета:

```bash
dpkg -S /usr/lib/x86_64-linux-gnu/libpq.so.5
libpq5

dpkg -S /usr/lib/x86_64-linux-gnu/libyaml-cpp.so.0.7
libyaml-cpp0.7
```

Имена пакетов найдены - вписываем их в наш `control`:

```text
Package: pg-server
Section: utils
Priority: optional
Version: 0.0.2
Maintainer: Mochalov Anton <xoore4ik@gmail.com>
Architecture: amd64
Depends: libpq5, libyaml-cpp0.7
Description: Учимся собирать свой простой deb-пакет
```

Пересобираем и ставим на чистой машине ещё раз:

```bash
apt-get install /packages/pg-server-amd64-dpkg.deb

Reading package lists... Done
Building dependency tree... Done
Note, selecting 'pg-server' instead of '/packages/pg-server-amd64-dpkg.deb'
The following additional packages will be installed:
  libldap-2.5-0 libldap-common libpq5 libsasl2-2 libsasl2-modules libsasl2-modules-db libyaml-cpp0.7
The following NEW packages will be installed:
  libldap-2.5-0 libldap-common libpq5 libsasl2-2 libsasl2-modules libsasl2-modules-db libyaml-cpp0.7 pg-server
0 upgraded, 8 newly installed, 0 to remove and 2 not upgraded.
```

Бинго! apt прочитал `Depends:` из `control` и сам дотянул недостающее - среди новых пакетов наши `libpq5` и `libyaml-cpp0.7`, а `libldap` и `libsasl` - это уже зависимости самой `libpq`. Сервер теперь запускается и на чистой системе.

Но вписывать зависимости руками - скучно и ненадёжно: забыл одну библиотеку - и пакет сломается на чужой машине. Логичный вопрос: можно ли автоматизировать и это? Можно - об этом следующая глава.
# Автоматическая сборка проекта при помощи dpkg-buildpackage

Конечно, всё то, что мы делали ранее руками, так уже никто не делает - мы прошли этот путь, чтобы понять, что никакой магии в работе пакетов нет.
Современные инструменты заточены под автоматизацию сборки пакетов. Одним из самых популярных инструментов является `dh_make`. О нём есть несколько статей на habr, например [тут](https://habr.com/ru/companies/ua-hosting/articles/281983/) и [тут](https://habr.com/ru/articles/783076/). Я не хотел бы рассказывать о его работе и тонкостях настройки, так как статьи выше сделали это за меня. Я лишь покажу, что всё то, что мы создавали руками, умеет автоматизировать `dh_make`.

Для начала поставим инструменты: `dh_make` генерирует заготовку пакета, `debhelper` - набор помощников, которые эту заготовку собирают, а `debuild` - обёртка над `dpkg-buildpackage`, которая управляет всей сборкой:

```bash
sudo apt-get install -y dh-make debhelper devscripts
```

Создаём наш debian-пакет:
```bash
dh_make -p pg-server_0.0.1 --createorig
```
По итогу получим следующую структуру, именно здесь мы и будем описывать наши systemd-юниты, пост/пре команды и документацию:
```bash
.
└── debian
    ├── changelog
    ├── control
    ├── copyright
    ├── manpage.1.ex
    ├── manpage.md.ex
    ├── manpage.sgml.ex
    ├── manpage.xml.ex
    ├── pg-server.cron.d.ex
    ├── pg-server.doc-base.ex
    ├── pg-server-docs.docs
    ├── postinst.ex
    ├── postrm.ex
    ├── preinst.ex
    ├── prerm.ex
    ├── README.Debian
    ├── README.source
    ├── rules
    ├── salsa-ci.yml.ex
    ├── source
    │   └── format
    └── watch.ex
```

Файлы с расширением `.ex` - это заготовки-примеры (manpage, cron, ci-конфиг и прочее). Они не используются, пока вы сами не переименуете их без суффикса, поэтому спокойно удаляем всё лишнее и оставляем только то, что нам действительно нужно.

Посмотрим на `debian/control`. Он устроен чуть иначе, чем наш ручной манифест, - разделён на две секции:

- **Source** - исходный проект и требования для его сборки;
- **Package** - описание устанавливаемого бинарного пакета.

```text
Source: pg-server
Section: net
Priority: optional
Maintainer: Student Developer <student@example.com>
Build-Depends:
 debhelper-compat (= 13),
 g++,
 make,
 pkg-config,
 libpq-dev,
 libyaml-cpp-dev
Standards-Version: 4.6.0
Rules-Requires-Root: no

Package: pg-server
Architecture: any
Depends:
 ${shlibs:Depends},
 ${misc:Depends}
Description: Small HTTP server reporting the libpq version
 A small educational HTTP server written in C++.
 It reads the listening port from a YAML configuration file
 and reports the PostgreSQL client library version.
```

Здесь `Build-Depends` перечисляет то, что нужно для *сборки* пакета (компилятор, заголовочные файлы библиотек), а вот `Depends` мы указываем не руками, а заготовками-переменными:

```text
Depends:
 ${shlibs:Depends},
 ${misc:Depends}
```

В момент сборки `dpkg-shlibdeps` просканирует наш бинарник, выяснит, с какими библиотеками он слинкован, найдёт пакеты-владельцы этих библиотек и запишет список в файл `./debian/pg-server.substvars`. А `debhelper` в конце подставит этот список на место `${shlibs:Depends}` в итоговом `control`. Важный нюанс: если бы в `debian/control` не было `${shlibs:Depends}`, ничего бы само не вписалось - переменная должна быть объявлена заранее. Это не магия, а простая подстановка.

Но откуда `dpkg-shlibdeps` знает, где лежит наш бинарник? Из `debian/pg-server.install` - файла, который описывает, какой файл в какой каталог устанавливать, по шаблону:

```text
исходный_файл каталог_назначения_в_пакете
```

В нашем случае:

```bash
vi ./debian/pg-server.install

pg-server /usr/bin
```

Обратите внимание: бинарник кладётся в `/usr/bin` - это та самая конвенция Debian, о которой мы говорили в «Дано»: содержимое пакетов ставится в `/usr`, а `/usr/local` остаётся территорией ручной установки администратором. Заодно эта же строка отвечает и на вопрос, как `dpkg-shlibdeps` найдёт бинарник для сканирования.

За файл `lib/systemd/system/pg-server.service` отвечает отдельный специализированный файл - `debian/pg-server.service`: debhelper увидит его и сам положит юнит в пакет. Юнит нам уже знаком - он лежал у нас в проекте с самого начала, поэтому просто копируем:

```bash
cp pg-server.service ./debian/pg-server.service
```

Содержимое `debian/rules` - правила сборки. Именно здесь определяется, что и в какой последовательности выполняется:

```bash
vi ./debian/rules

#!/usr/bin/make -f

export DEB_BUILD_MAINT_OPTIONS = hardening=+all
include /usr/share/dpkg/buildflags.mk

%:
	dh $@ --buildsystem=makefile

override_dh_auto_install:
```

Пустой `override_dh_auto_install` - не случайность: он отключает дефолтный `make install`, и тогда debhelper раскладывает файлы по описанию из `pg-server.install`, а не по тому, что прописано в нашем Makefile.

Остался ещё один файл - `debian/changelog`. И он важнее, чем кажется: в этой схеме версию пакета задаёт именно он, а не `Version:` в `control`. Проверить, какую версию увидит сборщик, можно так:

```bash
dpkg-parsechangelog -S Version
```

Заметьте: сборщик здесь очень капризный и ругается на любое отличие от эталонного формата changelog.

Нам остаётся выполнить `debuild -us -uc -b` и наблюдать магию:
```bash
debuild -us -uc -b
```
Итоговая структура проекта будет следующая:
```bash
./debian/pg-server/
├── DEBIAN
│   ├── control
│   ├── md5sums
│   ├── postinst
│   ├── postrm
│   └── prerm
├── lib
│   └── systemd
│       └── system
│           └── pg-server.service
└── usr
    ├── bin
    │   └── pg-server
    └── share
        └── doc
            └── pg-server
                ├── changelog.Debian.gz
                ├── copyright
                └── README.Debian
```
И здесь мы видим, что debuild сделал всё то, что ранее делали мы сами - он аккуратно разложил файлы в нужные каталоги, самостоятельно вписал `Depends` в control-файл - пакет создан и готов к запуску.

# Как скачать наш дистрибутив с удаленного репозитория

На протяжении этого небольшого урока мы собирали пакет на машине и на ней же его устанавливали. Но нам, конечно же, хочется разместить наш пакетик в репозитории артефактов и раздавать оттуда. Для этого в проекте есть `docker-compose.yml` с запуском `nexus`:
Часть разворачивания nexus и его настройки опустим, ведь есть прекрасная статья на эту тему [здесь](https://vc.ru/dev/1671041-ustanovka-i-nastroika-nexus-i-jenkins-ili-kak-my-nastroili-process-ci).
Условимся, что на данном этапе вы запустили nexus локально на порту :8081, создали репозиторий артефактов apt и пользователя для скачивания дистрибутивов.
```bash
docker compose up -d
```

Теперь наш пакет должен появиться в репозитории. Проще всего залить его через UI: в Nexus откроем Upload → выберем наш apt-репозиторий (`my-debian`) → добавим `pg-server-amd64-dpkg.deb` → Upload.

Чтобы пакет стал доступен `apt-get`, сообщим системе адрес нашего репозитория. Список репозиториев apt берёт из `/etc/apt/sources.list.d/` - положим туда файл `nexus.list`:

```bash
echo 'deb [trusted=yes] http://localhost:8081/repository/my-debian/ jammy main' | sudo tee /etc/apt/sources.list.d/nexus.list
```

Конечно же, для наших приватных пакетов нужны логин и пароль. Для этого в Linux есть специальная папка: `/etc/apt/auth.conf.d/`. При попытке скачать какой-либо дистрибутив Linux найдёт подходящий хост среди файлов папки `/etc/apt/auth.conf.d/`:

```
sudo vi /etc/apt/auth.conf.d/nexus.conf

machine localhost:8081
login <NEXUS-USER>
password <NEXUS-PASSWORD>
```

Теперь можем смело выполнять `sudo apt-get update` - Linux заглянет в наш репозиторий и сохранит список доступных пакетов, после чего нам станет доступен `apt-get install pg-server`. Проверим заветную команду на чистой машине:

```bash
sudo apt-get update
sudo apt-get install pg-server
```

Пакет, который мы час назад собирали руками, ставится из нашего репозитория одной командой - без пути к `.deb`, без `remove` перед установкой и на любой машине, у которой есть доступ к Nexus.

# Благодарность

Вот и всё! Спасибо большое, что дошли до этой главы, - значит, что-то интересное всё-таки было :)
Теперь мы с Вами знаем чуть больше про магию deb-пакетов в Linux.

Спасибо большое за прочтение этой скромной статьи, буду рад комментариям под постом 🙌