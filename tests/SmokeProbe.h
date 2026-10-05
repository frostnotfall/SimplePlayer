#pragma once
class QGuiApplication;
class QCommandLineParser;
class QQmlApplicationEngine;
namespace bp { class PlayerController; }
// Own validation timers and their captured state until the event loop exits.
int runPlayerEventLoop(QGuiApplication&,const QCommandLineParser&,bp::PlayerController&,QQmlApplicationEngine&);
