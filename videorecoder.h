#ifndef VIDEORECODER_H
#define VIDEORECODER_H

#include <QtCore/qglobal.h>
#include <QObject>
#include <QMap>


#ifdef VIDEORECODER_LIB
# define VIDEORECODER_EXPORT Q_DECL_EXPORT
#else
# define VIDEORECODER_EXPORT Q_DECL_IMPORT
#endif

class ScreenRecordImpl;

class VIDEORECODER_EXPORT VideoRecoder : public QObject
{
	Q_OBJECT
public:
	/*
	功能：开始录视频
	参数：pid  进程的id
	参数：strSaveFileName 保存的文件路径 如D:/1.mp4
	参数：width  保存的画面的宽度
	参数：height  保存的画面的高度
	参数：fps     帧率
	返回值：  0为成功  其他值为失败
	*/
	int startRecord(unsigned int pid, const QString& strSaveFileName, int width=1920, int height=1080, int fps=25);
	/*
	功能：暂停录视频
	参数：pid  进程的id
	返回值：  0为成功  其他值为失败
	*/
	int puaseRecord(unsigned int pid);
	/*
	功能：结束录视频
	参数：pid  进程的id
	返回值：  0为成功  其他值为失败
	*/
	int stopRecord(unsigned int pid);
	/*
	功能：恢复录视频
	参数：pid  进程的id
	返回值：  0为成功  其他值为失败
	*/
	int resumeRecord(unsigned int pid);

private:
	VideoRecoder(QObject * parent=nullptr);
	~VideoRecoder();

public:
	/*
	功能：获取实例
	返回值：  录制实例
	*/
	static VideoRecoder* getInstance();
private:
	static VideoRecoder* s_pInstance;
private:
	QMap<unsigned int, ScreenRecordImpl*> m_recoderImps;
};

#endif // VIDEORECODER_H
