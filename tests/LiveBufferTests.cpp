#include <QtTest>
#include "media/live/FlvBuffer.h"
#include "domain/Models.h"
using namespace bp;
static QByteArray tag(int type,int ms,QByteArray payload){
    QByteArray t(11,0);t[0]=char(type);int n=payload.size();t[1]=char(n>>16);t[2]=char(n>>8);t[3]=char(n);
    t=FlvBuffer::rebase(t,ms);t+=payload;n+=11;t+=char(n>>24);t+=char(n>>16);t+=char(n>>8);t+=char(n);return t;
}
static QByteArray header(){return QByteArray::fromHex("464c5601050000000900000000");}
class LiveBufferTests:public QObject {
    Q_OBJECT
private slots:
    void incrementalKeyframes(){
        auto config=tag(9,0,QByteArray::fromHex("170000000001"));
        auto data=header()+config+tag(9,5000,QByteArray::fromHex("170100000001"))+tag(9,6000,QByteArray::fromHex("270100000002"))+tag(9,7000,QByteArray::fromHex("170100000003"));
        FlvBuffer b;for(char c:data)b.feed(QByteArray(1,c));
        QVERIFY(b.ready());QCOMPARE(b.start(),0.0);QCOMPARE(b.end(),2.0);
        QCOMPARE(b.point(1.5)->ms,0);QCOMPARE(b.point(99)->ms,2000);
        QCOMPARE(b.point(-2)->prefix,header()+config);QVERIFY(!b.point(qQNaN()));
        auto rebased=FlvBuffer::rebase(b.point(2)->bytes,333);QCOMPARE(quint8(rebased[5]),1);QCOMPARE(quint8(rebased[6]),77);
        QCOMPARE(rebased.mid(11),b.point(2)->bytes.mid(11));
    }
    void evictionAtKeyframe(){
        FlvBuffer b(4096,1500);b.feed(header());quint64 first=0;
        for(int i=0;i<6;++i){b.feed(tag(9,i*1000,QByteArray::fromHex("170100000001")));if(!i)first=b.point(0)->sequence;}
        QVERIFY(!b.packet(first));QCOMPARE(b.start(),4.0);QCOMPARE(b.end(),5.0);QCOMPARE(b.point(0)->ms,4000);
    }
    void memoryLimitAndAudioOnly(){
        FlvBuffer b(100);b.feed(header());b.feed(tag(9,0,QByteArray::fromHex("170100000001")));
        b.feed(tag(9,1000,QByteArray(200,char(0x27))));QVERIFY(b.bytes()<=100);QVERIFY(!b.point(0));
        auto h=header();h[4]=4;FlvBuffer audio;audio.feed(h+tag(8,0,QByteArray::fromHex("af001210"))+tag(8,1000,QByteArray::fromHex("af0101"))+tag(8,1500,QByteArray::fromHex("af0102")));
        QVERIFY(audio.ready());QCOMPARE(audio.point(.5)->ms,500);
    }
    void invalidInput(){FlvBuffer b;QVERIFY_EXCEPTION_THROWN(b.feed(QByteArray("Not an FLV")),Error);FlvBuffer c;auto bad=tag(9,0,QByteArray::fromHex("170100000001"));bad[bad.size()-1]=0;QVERIFY_EXCEPTION_THROWN(c.feed(header()+bad),Error);}
};
QTEST_GUILESS_MAIN(LiveBufferTests)
#include "LiveBufferTests.moc"
