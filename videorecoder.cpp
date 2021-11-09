#include "videorecoder.h"
#include "ScreenRecordImpl.h"
#include "ErrorDef.h"


VideoRecoder* VideoRecoder::s_pInstance = nullptr;


VideoRecoder::VideoRecoder(QObject * parent) :QObject(parent)
{
	m_recoderImps.clear();
}

VideoRecoder::~VideoRecoder()
{
	QMap<unsigned int, ScreenRecordImpl*>::iterator iter = m_recoderImps.begin();
	for (; iter != m_recoderImps.end(); )
	{
		if(iter.value() != nullptr)
		{
			iter.value()->Stop();
			iter = m_recoderImps.erase(iter);
		}
		else
			++iter;
	}
}

int VideoRecoder::startRecord(unsigned int pid, const QString& strSaveFileName, int width, int height, int fps)
{
	if (m_recoderImps.contains(pid))
	{
		return RECORD_ERROR_ALREADY_EXIST;                          //�����ظ����
	}
	ScreenRecordImpl* imp = new ScreenRecordImpl();
	if(!imp)
		return RECORD_ERROR_INS_FAILURE;                          //����ʧ��
	int iRet = imp->Start(pid, strSaveFileName, width, height, fps);
	if(iRet == 0)
	{
		m_recoderImps.insert(pid, imp);
	}
	return iRet;
}


int VideoRecoder::puaseRecord(unsigned int pid)
{
	if (!m_recoderImps.contains(pid))
	{
		return RECORD_ERROR_DONNOT_EXIST;                          //�����ڴ˽���id
	}
	return m_recoderImps[pid]->Pause();
}


int VideoRecoder::stopRecord(unsigned int pid)
{
	if (!m_recoderImps.contains(pid))
	{
		return RECORD_ERROR_DONNOT_EXIST;                          //�����ڴ˽���id
	}
	m_recoderImps[pid]->Stop();
	ScreenRecordImpl* imp  = m_recoderImps.take(pid);
	if(imp)
	{
		delete imp;
		imp = nullptr;
	}
	return 0;
}


int VideoRecoder::resumeRecord(unsigned int pid)
{
	if (!m_recoderImps.contains(pid))
	{
		return RECORD_ERROR_DONNOT_EXIST;                          //�����ڴ˽���id
	}
	return m_recoderImps[pid]->Resume();
}


VideoRecoder* VideoRecoder::getInstance()
{
	if(s_pInstance == nullptr)
	{
		s_pInstance = new VideoRecoder();
	}
	return s_pInstance;
}


