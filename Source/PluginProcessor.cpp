/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <Windows.h>
#include <fstream>
#include "AudioWork.h"
using namespace std::chrono;

//==============================================================================
NetProcessJUCEVersionAudioProcessor::NetProcessJUCEVersionAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
	loadConfig();
}

NetProcessJUCEVersionAudioProcessor::~NetProcessJUCEVersionAudioProcessor()
{
	loadConfig();
}

//==============================================================================
const juce::String NetProcessJUCEVersionAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool NetProcessJUCEVersionAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool NetProcessJUCEVersionAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool NetProcessJUCEVersionAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double NetProcessJUCEVersionAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int NetProcessJUCEVersionAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int NetProcessJUCEVersionAudioProcessor::getCurrentProgram()
{
    return 0;
}

void NetProcessJUCEVersionAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String NetProcessJUCEVersionAudioProcessor::getProgramName (int index)
{
    return {};
}

void NetProcessJUCEVersionAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void NetProcessJUCEVersionAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
	// reset state and buffer
	iNumberOfChanel = 1;
	lNoOutputCount = 0;
	bDoItSignal = false;

	// 前导缓冲区缓存初始化
	// 准备20s的缓冲区
	lPrefixBufferSize = static_cast<long>(20.0f * sampleRate);
	// 前导缓冲长度
	lPrefixLengthSampleNumber = static_cast<long>(fPrefixLength * sampleRate);
	fPrefixBuffer = (float*)std::malloc(sizeof(float) * lPrefixBufferSize);
	lPrefixBufferPos = 0;

	lModelOutputBufferSize = static_cast<long>(fModelOutputBufferSecond * sampleRate);
	fModeulOutputSampleBuffer = (float*)(std::malloc(sizeof(float) * lModelOutputBufferSize));
	lModelOutputSampleBufferReadPos = 0;
	lastVoiceSampleCrossFadeSkipNumber = 0;
	lModelOutputSampleBufferWritePos = 0;

	modelInputJobList.clear();
	prepareModelInputJob.clear();

	// worker线程安全退出相关信号
	bWorkerNeedExit = false;
	kRecordState = IDLE;
	runWorker();
}

void NetProcessJUCEVersionAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
	bWorkerNeedExit = true;
	// 当子线程还在运行时，这个锁是锁上的，此时主线程还不能退出
	// 主线程通过将退出信号发给子线程，等待子线程安全退出后，释放锁，主线程再退出
	mWorkerSafeExit.lock();
	mWorkerSafeExit.unlock();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool NetProcessJUCEVersionAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void NetProcessJUCEVersionAudioProcessor::processBlock (juce::AudioBuffer<float>& audioBuffer, juce::MidiBuffer& midiMessages)
{
	if (bConfigLoadFinished) {
		juce::ScopedNoDenormals noDenormals;
		auto totalNumInputChannels = getTotalNumInputChannels();
		auto totalNumOutputChannels = getTotalNumOutputChannels();

		// In case we have more outputs than inputs, this code clears any output
		// channels that didn't contain input data, (because these aren't
		// guaranteed to be empty - they may contain garbage).
		// This is here to avoid people getting screaming feedback
		// when they first compile a plugin, but obviously you don't need to keep
		// this code if your algorithm always overwrites all the output channels.
		for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i) {
			audioBuffer.clear(i, 0, audioBuffer.getNumSamples());
		}

		double fSampleMax = -9999;
		float* inputOutputL = audioBuffer.getWritePointer(0);
		float* inputOutputR = NULL;
		bool bHasRightChanel = false;
		if (getNumOutputChannels() > 1) {
			inputOutputR = audioBuffer.getWritePointer(1);
			bHasRightChanel = true;
		}
		else {
			inputOutputR = inputOutputL;
		}
		for (juce::int32 i = 0; i < audioBuffer.getNumSamples(); i++) {
			// 获取当前块的最大音量
			float fCurrentSample = inputOutputL[i];
			float fSampleAbs = std::abs(fCurrentSample);
			if (fSampleAbs > fSampleMax) {
				fSampleMax = fSampleAbs;
			}
		}

		if (bRealTimeMode) {
			// 如果启用了实时模式，则无需检测音量阈值
			bVolumeDetectFine = true;
		}
		else {
			bVolumeDetectFine = fSampleMax >= fSampleVolumeWorkActiveVal;
		}

		if (bVolumeDetectFine) {
			fRecordIdleTime = 0.f;
		}
		else {
			fRecordIdleTime += 1.f * audioBuffer.getNumSamples() / getSampleRate();
			/*if (bEnableDebug) {
				snprintf(buff, sizeof(buff), "当前累积空闲时间:%f\n", fRecordIdleTime);
				OutputDebugStringA(buff);
			}*/
		}

		if (kRecordState == IDLE) {
			// 当前是空闲状态
			if (bVolumeDetectFine) {
				/*if (bEnableDebug) {
					OutputDebugStringA("切换到工作状态");
				}*/
				kRecordState = WORK;

				// 从IDLE切换到WORD，表示需要一段新的模型输入
				if (bRealTimeMode && fPrefixLength > 0.01f) {
					// 实时模式
					// 将前导缓冲区的数据写入到模型入参缓冲区中
					// 从前导缓冲区当前位置向前寻找lPrefixLengthSampleNumber个数据 
					int readPosStart = lPrefixBufferPos - lPrefixLengthSampleNumber;
					int readPosEnd = lPrefixBufferPos;
					if (readPosStart >= 0) {
						// 场景1：[.....start......end...]，直接从中间读取需要的数据
						for (int i = readPosStart; i < readPosEnd; i++) {
							prepareModelInputJob.push_back(fPrefixBuffer[i]);
						}
					}
					else {
						// 场景2：[.....end......start...]，需要从循环缓冲区尾部获取一些数据，然后再从头部读取一些数据
						readPosStart = lPrefixBufferSize + readPosStart - 1;
						for (int i = readPosStart; i < lPrefixBufferSize; i++) {
							prepareModelInputJob.push_back(fPrefixBuffer[i]);
						}
						for (int i = 0; i < readPosEnd; i++) {
							prepareModelInputJob.push_back(fPrefixBuffer[i]);
						}
					};
				};
				
				// 将当前的音频数据写入到模型输入缓冲区中
				for (int i = 0; i < audioBuffer.getNumSamples(); i++) {
					prepareModelInputJob.push_back(inputOutputL[i]);
				};
			}
		}
		else {
			// 当前是工作状态
			// 只需要持续将当前的音频数据写入到模型入参缓冲区中
			for (int i = 0; i < audioBuffer.getNumSamples(); i++) {
				prepareModelInputJob.push_back(inputOutputL[i]);
			}

			// 判断是否需要退出工作状态
			bool bExitWorkState = false;

			// 退出条件1：音量过小且持续超过一定时间
			if (fRecordIdleTime >= fLowVolumeDetectTime) {
				bExitWorkState = true;
				/*if (bEnableDebug) {
					OutputDebugStringA("音量过小且持续超过一定时间，直接调用模型\n");
				}*/
			}

			// 退出条件2：队列达到一定的大小
			//long inputBufferSize = func_cacl_read_write_buffer_data_size(lModelInputOutputBufferSize, lModelInputSampleBufferReadPos, lModelInputSampleBufferWritePos);
			long inputBufferSize = prepareModelInputJob.size();
			if (inputBufferSize > lMaxSliceLengthSampleNumber + lPrefixLengthSampleNumber) {
				bExitWorkState = true;
				/*if (bEnableDebug) {
					OutputDebugStringA("队列大小达到预期，直接调用模型\n");
				}*/
			}

			if (bExitWorkState) {
				// 需要退出工作状态
				// 1.模型输入队列加锁
				// 2.将当前准备的模型输入放入队列
				// 3.准备一个新的模型输入供下一次使用
				// 4.设置标记位，供消费者检查
				// 5.模型输入队列锁释放
				//long long tStart = func_get_timestamp();
				modelInputJobListMutex.lock();
				modelInputJobList.push_back(prepareModelInputJob);
				std::vector<float> newPrepareModelInputJob;
				prepareModelInputJob = newPrepareModelInputJob;
				kRecordState = IDLE;
				bDoItSignal = true;
				modelInputJobListMutex.unlock();
				/*long long tUseTime = func_get_timestamp() - tStart;
				if (bEnableDebug) {
					snprintf(buff, sizeof(buff), "音频线程锁耗时:%lldms\n", tUseTime);
					OutputDebugStringA(buff);
				}*/
			}
		}

		// 将当前的音频数据写入到前导缓冲区中，供下一次使用
		for (int i = 0; i < audioBuffer.getNumSamples(); i++) {
			fPrefixBuffer[lPrefixBufferPos++] = inputOutputL[i];
			if (lPrefixBufferPos == lPrefixBufferSize) {
				lPrefixBufferPos = 0;
			}
		}

		// 如果模型输出缓冲区还有数据的话，写入到输出信号中去
		int outputWritePos = 0;
		bool bHasMoreData = lModelOutputSampleBufferReadPos != lModelOutputSampleBufferWritePos;
		if (bHasMoreData) {
			for (int i = 0; i < audioBuffer.getNumSamples(); i++)
			{
				bHasMoreData = lModelOutputSampleBufferReadPos != lModelOutputSampleBufferWritePos;
				if (!bHasMoreData) {
					break;
				}
				double currentSample = fModeulOutputSampleBuffer[lModelOutputSampleBufferReadPos++];
				if (lModelOutputSampleBufferReadPos == lModelOutputBufferSize) {
					lModelOutputSampleBufferReadPos = 0;
				}
				inputOutputL[i] = static_cast<float>(currentSample);
				if (bHasRightChanel) {
					inputOutputR[i] = static_cast<float>(currentSample);
				};
				outputWritePos = i + 1;
			}
		}
		// 判断当前是否有空输出
		bool hasEmptyBlock = outputWritePos != audioBuffer.getNumSamples();
		if (hasEmptyBlock) {
			if (bRealTimeMode) {
				// 实时模式
				// 从“待交叉淡化数据”中先取出一部分数据
				int blockRemainNeedSampleNumber = audioBuffer.getNumSamples() - outputWritePos;
				lastVoiceSampleForCrossFadeVectorMutex.lock();
				int peekDataSize = min(blockRemainNeedSampleNumber, lastVoiceSampleForCrossFadeVector.size());
				//peekDataSize = 0;
				lastVoiceSampleCrossFadeSkipNumber += peekDataSize;
 				if (peekDataSize > 0) {
					if (bEnableDebug) {
						snprintf(buff, sizeof(buff), "!!!!!!!!!!!!!!!!!!!!!!!实时模式-使用交叉淡化数据提前输出，避免空输出:%ld\n", lNoOutputCount);
						OutputDebugStringA(buff);
					}
					for (int i = 0; i < peekDataSize; i++) {
						auto peekSample = lastVoiceSampleForCrossFadeVector.at(0);
						lastVoiceSampleForCrossFadeVector.erase(lastVoiceSampleForCrossFadeVector.begin());
						inputOutputL[outputWritePos] = static_cast<float>(peekSample);
						if (bHasRightChanel) {
							inputOutputR[outputWritePos] = static_cast<float>(peekSample);
						};
						outputWritePos++;
					}
				};
				lastVoiceSampleForCrossFadeVectorMutex.unlock();
				hasEmptyBlock = outputWritePos != audioBuffer.getNumSamples();
				if (hasEmptyBlock) {
					// 还有空输出，此时已经没有数据可以用了
					// 输出静音
					lNoOutputCount += 1;
					if (bEnableDebug) {
						snprintf(buff, sizeof(buff), "!!!!!!!!!!!!!!!!!!!!!!!实时模式-输出缓冲空:%ld\n", lNoOutputCount);
						OutputDebugStringA(buff);
					}
					for (juce::int32 i = outputWritePos; i < audioBuffer.getNumSamples(); i++) {
						// 对输出静音
						inputOutputL[i] = 0.0000000001f;
						if (bHasRightChanel) {
							inputOutputR[i] = 0.0000000001f;
						}
					}
				}
			}
			else {
				// 分句模式
				// 无数据可以取了，输出静音
				lNoOutputCount += 1;
				if (bEnableDebug) {
					snprintf(buff, sizeof(buff), "!!!!!!!!!!!!!!!!!!!!!!!分句模式-输出缓冲空:%ld\n", lNoOutputCount);
					OutputDebugStringA(buff);
				}
				for (juce::int32 i = outputWritePos; i < audioBuffer.getNumSamples(); i++) {
					// 对输出静音
					inputOutputL[i] = 0.0000000001f;
					if (bHasRightChanel) {
						inputOutputR[i] = 0.0000000001f;
					}
				}
			}
		}
	}
}

//==============================================================================
bool NetProcessJUCEVersionAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* NetProcessJUCEVersionAudioProcessor::createEditor()
{
    return new NetProcessJUCEVersionAudioProcessorEditor (*this);
}

//==============================================================================
void NetProcessJUCEVersionAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
	std::unique_ptr<juce::XmlElement> xml(new juce::XmlElement("Config"));
	xml->setAttribute("fMaxSliceLength", (double)fMaxSliceLength);
	xml->setAttribute("fMaxSliceLengthForRealTimeMode", (double)fMaxSliceLengthForRealTimeMode);
	xml->setAttribute("fMaxSliceLengthForSentenceMode", (double)fMaxSliceLengthForSentenceMode);
	xml->setAttribute("fLowVolumeDetectTime", (double)fLowVolumeDetectTime);
	xml->setAttribute("fPrefixLength", (double)fPrefixLength);
	xml->setAttribute("fDropSuffixLength", (double)fDropSuffixLength);
	xml->setAttribute("fPitchChange", (double)fPitchChange);
	xml->setAttribute("bRealTimeMode", bRealTimeMode);
	xml->setAttribute("bEnableDebug", bEnableDebug);
	xml->setAttribute("iSelectRoleIndex", iSelectRoleIndex);
	copyXmlToBinary(*xml, destData);
}

void NetProcessJUCEVersionAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{	
	// load last state
	std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

	if (xmlState.get() != nullptr)
		if (xmlState->hasTagName("Config")) {
			fMaxSliceLength = (float)xmlState->getDoubleAttribute("fMaxSliceLength", 1.0);
			fMaxSliceLengthForRealTimeMode = (float)xmlState->getDoubleAttribute("fMaxSliceLengthForRealTimeMode", 1.0);
			fMaxSliceLengthForSentenceMode = (float)xmlState->getDoubleAttribute("fMaxSliceLengthForSentenceMode", 1.0);
			lMaxSliceLengthSampleNumber = static_cast<long>(getSampleRate() * fMaxSliceLength);
			fLowVolumeDetectTime = (float)xmlState->getDoubleAttribute("fLowVolumeDetectTime", 0.4);
			fPrefixLength = (float)xmlState->getDoubleAttribute("fPrefixLength", 0.0);
			fDropSuffixLength = (float)xmlState->getDoubleAttribute("fDropSuffixLength", 0.0);
			fPitchChange = (float)xmlState->getDoubleAttribute("fPitchChange", 1.0);
			bRealTimeMode = (bool)xmlState->getBoolAttribute("bRealTimeMode", false);
			bEnableDebug = (bool)xmlState->getBoolAttribute("bEnableDebug", false);
			iSelectRoleIndex = (int)xmlState->getIntAttribute("iSelectRoleIndex", 0);
		}
}

void NetProcessJUCEVersionAudioProcessor::loadConfig()
{
	std::wstring sDllPath = L"C:/Program Files/Common Files/VST3/NetProcessJUCEVersion/samplerate.dll";
	std::string sJsonConfigFileName = "C:/Program Files/Common Files/VST3/NetProcessJUCEVersion/netProcessConfig.json";
	
	if (!bConfigLoadFinished) {
		// default value
		fMaxSliceLength = 5.0f;
		fMaxSliceLengthForRealTimeMode = fMaxSliceLength;
		fMaxSliceLengthForSentenceMode = fMaxSliceLength;
		fLowVolumeDetectTime = 0.4f;
		fPrefixLength = 0.0f;
		fDropSuffixLength = 0.0f;
		lMaxSliceLengthSampleNumber = static_cast<long>(getSampleRate() * fMaxSliceLength);
		fPitchChange = 0.0f;
		bRealTimeMode = false;
		bEnableDebug = false;
		iSelectRoleIndex = 0;

		auto dllClient = LoadLibraryW(sDllPath.c_str());
		if (dllClient != NULL) {
			dllFuncSrcSimple = (FUNC_SRC_SIMPLE)GetProcAddress(dllClient, "src_simple");
		}
		else {
			OutputDebugStringA("samplerate.dll load Error!");
		}

		// 读取JSON配置文件
		std::ifstream t_pc_file(sJsonConfigFileName, std::ios::binary);
		std::stringstream buffer_pc_file;
		buffer_pc_file << t_pc_file.rdbuf();

		juce::var jsonVar;
		if (juce::JSON::parse(buffer_pc_file.str(), jsonVar).wasOk()) {
			auto& props = jsonVar.getDynamicObject()->getProperties();
			bEnableSOVITSPreResample = props["bEnableSOVITSPreResample"];
			iSOVITSModelInputSamplerate = props["iSOVITSModelInputSamplerate"];
			bEnableHUBERTPreResample = props["bEnableHUBERTPreResample"];
			iHUBERTInputSampleRate = props["iHUBERTInputSampleRate"];
			fSampleVolumeWorkActiveVal = props["fSampleVolumeWorkActiveVal"];

			roleList.clear();
			auto jsonRoleList = props["roleList"];
			int iRoleSize = jsonRoleList.size();
			for (int i = 0; i < iRoleSize; i++) {
				auto& roleListI = jsonRoleList[i].getDynamicObject()->getProperties();
				std::string apiUrl = roleListI["apiUrl"].toString().toStdString();
				std::string name = roleListI["name"].toString().toStdString();
				std::string speakId = roleListI["speakId"].toString().toStdString();
				roleStruct role;
				role.sSpeakId = speakId;
				role.sName = name;
				role.sApiUrl = apiUrl;
				roleList.push_back(role);
			}
			if (iSelectRoleIndex + 1 > iRoleSize || iSelectRoleIndex < 0) {
				iSelectRoleIndex = 0;
			}
		}
		else {
			// error read json
		};

		bConfigLoadFinished = true;
	}
}

void NetProcessJUCEVersionAudioProcessor::runWorker()
{
    // 启动Worker线程
    std::thread(func_do_voice_transfer_worker,
        iNumberOfChanel,					// 通道数量
        getSampleRate(),		            // 项目采样率

		&modelInputJobList,					// 模型输入队列
		&modelInputJobListMutex,			// 模型输入队列锁

		lModelOutputBufferSize,				// 模型输出缓冲区大小
        fModeulOutputSampleBuffer,			// 模型输出缓冲区
        &lModelOutputSampleBufferReadPos,	// 模型输出缓冲区读指针
        &lModelOutputSampleBufferWritePos,	// 模型输出缓冲区写指针

		&lastVoiceSampleForCrossFadeVectorMutex,
		&lastVoiceSampleForCrossFadeVector, //最后一条模型输出音频的尾部，用于交叉淡化处理
		&lastVoiceSampleCrossFadeSkipNumber,

		&fPrefixLength,						// 前导缓冲区时长(s)
		&fDropSuffixLength,					// 丢弃的尾部时长(s)
        &fPitchChange,						// 音调变化数值
        &bCalcPitchError,					// 启用音调误差检测

        roleList,							// 配置的可用音色列表
        &iSelectRoleIndex,					// 选择的角色ID
        dllFuncSrcSimple,					// DLL内部SrcSimple方法

        &bEnableSOVITSPreResample,			// 启用SOVITS模型入参音频重采样预处理
        iSOVITSModelInputSamplerate,		// SOVITS模型入参采样率
        &bEnableHUBERTPreResample,			// 启用HUBERT模型入参音频重采样预处理
        iHUBERTInputSampleRate,				// HUBERT模型入参采样率

        &bRealTimeMode,					    // 占位符，实时模式
        &bDoItSignal,						// 占位符，表示该worker有待处理的数据
		&bEnableDebug,						// 占位符，启用DEBUG输出
		vServerUseTime,						// UI变量，显示服务调用耗时
		vDropDataLength,					// UI变量，显示实时模式下丢弃的音频数据长度

        &bWorkerNeedExit,					// 占位符，表示worker线程需要退出
        &mWorkerSafeExit					// 互斥锁，表示worker线程已经安全退出
    ).detach();


}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NetProcessJUCEVersionAudioProcessor();
}
