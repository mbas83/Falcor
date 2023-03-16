from falcor import *
import json

# use time in seconds for capturing, fps varies between render passes, change for each scene
captureTime = 60

graph = m.getGraph("PureRTShadows")
deferredPass = graph.getPass("DeferredRTShadows")

# capture video, only works wirh RAW??
#m.videoCapture.outputDir = "D:\\"
#m.videoCapture.baseFilename = "CapturedVideo"
#m.videoCapture.codec = Codec.RAW
#m.videoCapture.fps = 30
#m.videoCapture.bitrate = 4.0
#m.videoCapture.gopSize = 10
#m.videoCapture.addRanges(m.activeGraph, [[1, 200]])

m.clock.stop()
m.clock.play()
m.scene.animated = True
m.profiler.enabled = True
m.profiler.startCapture()


while m.clock.time < captureTime:
    renderFrame()


capturedData = m.profiler.endCapture()
m.profiler.enabled = False
    
with open('RT_Runtime_City.json', 'w') as fp:
    json.dump(capturedData, fp, indent=2)    
    
#m.profiler.events["/present/gpuTime"]["value"]

#meanFrameTime = capture["events"]["/onFrameRender/gpuTime"]["stats"]["mean"]
#print(meanFrameTime)

