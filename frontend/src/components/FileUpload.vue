<template>
  <div
    :class="[
      dragging && ota.state == 'upload'
        ? 'bg-yellow-500 text-white'
        : 'bg-gray-50 text-yellow-500',
    ]"
    class="
      flex flex-col
      pb-0
      h-52
      xl:h-52
      rounded-lg
      shadow-md
      tracking-wide
      border border-blue
      cursor-pointer
      ease-linear
      transition-all
      duration-150
    "
    @dragenter.prevent
    @dragover.prevent
    @drop.prevent="dropHandler"
    @dragover="dragging = true"
    @dragleave="dragging = false"
    @dragenter="dragEnterHandler"
  >
    <label
      name="upload"
      v-if="ota.state == 'upload' || ota.state == 'uploading'"
      class="
        flex flex-col
        pt-6
        items-center
        mb-5
        flex-1
        transition-all
        animate-div
      "
    >
      <i class="">
        <svg
          class="h-12 w-12 xl:h-12 xl:h-12"
          viewBox="0 0 48 48"
          fill="none"
          :stroke="[dragging && ota.state == 'upload' ? '#fff' : '#f85e20']"
          xmlns="http://www.w3.org/2000/svg"
        >
          <path
            d="M11.6777 20.271C7.27476 21.3181 4 25.2766 4 30C4 35.5228 8.47715 40 14 40V40C14.9474 40 15.864 39.8683 16.7325 39.6221"
            stroke-width="4"
            stroke-linecap="round"
            stroke-linejoin="round"
          />
          <path
            d="M36.0547 20.271C40.4577 21.3181 43.7324 25.2766 43.7324 30C43.7324 35.5228 39.2553 40 33.7324 40V40C32.785 40 31.8684 39.8683 30.9999 39.6221"
            stroke-width="4"
            stroke-linecap="round"
            stroke-linejoin="round"
          />
          <path
            d="M36 20C36 13.3726 30.6274 8 24 8C17.3726 8 12 13.3726 12 20"
            stroke-width="4"
            stroke-linecap="round"
            stroke-linejoin="round"
          />
          <path
            d="M17.0654 30.119L23.9999 37.0764L31.1318 30"
            stroke-width="4"
            stroke-linecap="round"
            stroke-linejoin="round"
          />
          <path
            d="M24 20V33.5382"
            stroke-width="4"
            stroke-linecap="round"
            stroke-linejoin="round"
          />
        </svg>
      </i>
      <span class="text-lg xl:text-3xl leading-normal">{{
        ota.state == 'upload'
          ? 'Select a firmware or drag it here'
          : 'Loading...'
      }}</span>

      <input v-if="ota.state != 'upload'" class="hidden" type="text" />
      <input
        name="file-input"
        v-if="ota.state == 'upload'"
        :id="id"
        type="file"
        class="hidden"
        :multiple="multiple"
        @change="handleUpload"
      />
      <small
        :class="`text-gray-600 flex overflow-ellipsis max-w-xl text-sm lg:text-sm`"
      >
        <slot name="file" :files="files" :uploadInfo="uploadInfo">{{
          uploadInfo
        }}</slot>
      </small>

      <button
        name="upload-btn"
        v-if="files.length"
        :class="[
          ota.state == 'upload'
            ? 'text-blue-500 border-blue-500 hover:bg-blue-500 active:bg-blue-600'
            : 'text-red-500 border-red-500 hover:bg-red-500 active:bg-red-600',
        ]"
        class="
          bg-transparent
          border border-solid
          hover:text-white
          font-bold
          text-sm
          mt-2
          px-6
          py-2
          rounded
          outline-none
          mr-1
          mb-1
          ease-linear
          transition-all
          duration-150
        "
        type="button"
        @click="onUploadBtn"
      >
        {{ ota.state == 'upload' ? 'Start' : 'Cancel' }}
      </button>
    </label>

    <div
      name="connect"
      v-if="ota.state == 'connect' || ota.state == 'connecting'"
      class="
        flex
        items-center
        flex-wrap
        content-center
        justify-center
        mb-3
        -mt-5
        pt-0
        flex-1
        transition-all
        animate-div
      "
    >
      <label for="address" class="mt-5 pr-5 text-gray-500">address</label>
      <div class="relative mt-5">
        <input
          type="text"
          spellcheck="false"
          name="ota address"
          v-model="serverInfo.address"
          :focus-visible="[ota.errMsg != '']"
          :class="[ota.errMsg != '' ? 'border-red-400' : 'border-gray-400']"
          class="
            font-mono
            px-3
            py-3
            placeholder-gray-400
            text-gray-600
            relative
            bg-white
            rounded
            text-sm
            border
            outline-none
            focus:outline-none focus:ring
          "
        />
        <div
          class="
            font-sans
            text-xs
            top-full
            pt-1
            text-red-500
            left-0
            absolute
            transition-all
          "
        >
          {{ ota.errMsg }}
        </div>
      </div>
      <button
        class="
          text-blue-500
          bg-transparent
          border border-solid border-blue-500
          hover:bg-blue-500 hover:text-white
          disabled:text-gray-500 disabled:bg-gray-50
          active:bg-blue-600
          font-bold
          uppercase
          text-sm
          mt-5
          px-5
          py-3
          rounded
          outline-none
          focus:outline-none
          mr-1
          ml-5
          ease-linear
          transition-all
          duration-150
        "
        type="button"
        @click="onAddressBtn"
      >
        Start !
      </button>
    </div>

    <label
      name="done"
      v-if="ota.state == 'done'"
      class="
        flex flex-col
        items-center
        justify-center
        flex-1
        transition-all
        animate-div
      "
    >
      <i class="flex items-center transition-all">
        <successAnimation id="ass" class="w-12 flex-1" />
        <span class="animate-text text-2xl sm:text-3xl leading-normal pl-5"
          >Done</span
        >
      </i>

      <input type="text" class="hidden" />

      <button
        class="
          animate-btn
          flex
          bg-transparent
          border border-solid
          hover:text-white
          font-bold
          text-blue-500
          border-blue-500
          hover:bg-blue-500
          active:bg-blue-600
          text-sm
          mt-9
          mr-1
          px-6
          py-2
          rounded
          outline-none
          ease-linear
          transition-all
          duration-150
        "
        type="button"
        @click="ota.state = 'connect'"
      >
        Back
      </button>
    </label>

    <div name="progress" :class="progressType" class="pb-0 mb-0">
      <div class="overflow-hidden h-1 mb-0 text-xs flex rounded bg-gray-50">
        <div
          :class="progressType"
          class="
            co-progress-bar
            transition-all
            shadow-none
            flex flex-col
            whitespace-nowrap
            justify-center
          "
        ></div>
      </div>
    </div>
  </div>
</template>


<style scoped>
.co-progress-bar {
  width: v-bind('progress');
}

.shim {
  position: relative;
  overflow: hidden;
  background-color: rgba(255, 255, 255, 0);
}
.shim::after {
  position: absolute;
  top: 0;
  right: 0;
  bottom: 0;
  left: 0;
  transform: translateX(-100%);
  background-image: linear-gradient(
    90deg,
    rgba(55, 66, 99, 1) 0,
    rgba(55, 66, 99, 1) 100%
  );
  animation: shimmer 1.5s ease-out infinite;
  content: '';
}

@keyframes shimmer {
  100% {
    transform: translateX(0%);
    opacity: 0;
  }
}

.animate-text {
  color: #4bb71b;
  animation: 1s slowFadeIn;
}

.animate-btn {
  animation-fill-mode: forwards;
  animation: slowFadeIn 1.3s;
}

@keyframes scale {
  0%,
  100% {
    transform: none;
  }

  50% {
    transform: scale3d(1.1, 1.1, 1);
  }
}

@keyframes slowFadeIn {
  0% {
    opacity: 0;
  }
  99% {
    opacity: 0;
  }
  100% {
    opacity: 0%;
  }
}

.animate-div {
  opacity: 0;

  animation: fadeIn 0.4s;
  animation-fill-mode: forwards;
}

@keyframes fadeIn {
  from {
    opacity: 0;
  }
  to {
    opacity: 1;
  }
}
</style>


<script>
import qs from 'qs'

import { watch, ref, computed, reactive } from 'vue'
import successAnimation from './success.vue'
export default {
  props: {
    id: { type: String, default: 'corsacOTA' },
  },
  components: {
    successAnimation,
  },
  setup(props, { emit }) {
    // address bind
    // -> ?address=192.168.1.2:3241
    const pageParams = new URL(document.URL).searchParams
    const pageAddressParams = pageParams.get('address')
    const serverInfo = reactive({
      address: pageAddressParams || 'OTA.local:3241',
    })

    //

    const files = ref([])
    const progress = ref('0%')
    const dragging = ref(false)

    const defaultChunkSize = 1024 * 12 // default : 12KB

    const ota = reactive({
      errMsg: '',
      state: 'connect',
      deviceType: '',

      totalSize: 0,
      chunkSize: defaultChunkSize,
      offset: 0,
      file: {},
      reader: {},
    })

    watch(
      () => ota.state,
      (newValue, oldValue) => {
        if (newValue == 'done') {
          playSuccessAudio()
        }
      }
    )

    watch(
      () => files.value,
      (newValue, oldValue) => {
        ota.errMsg = ''
      }
    )


    const progressType = computed(() => {
      if (ota.state == 'connecting') {
        return 'shim'
      } else if (ota.errMsg != '') {
        return 'bg-red-500'
      } else {
        return 'bg-blue-500'
      }
    })

    // display the uploaded file names and device type name
    const uploadInfo = computed(() => {
      if (ota.errMsg != '') {
        return ota.errMsg
      }
      return files.value.length === 1
        ? `${ota.deviceType != '' ? ota.deviceType + ' | ' : ''}${
            files.value[0].name
          }`
        : `${files.value.length} files selected`
    })

    const resetOTA = () => {
      ota.errMsg = ''
      ota.offset = 0
      ota.deviceType = ''
      ota.chunkSize = defaultChunkSize
      ota.reader = new FileReader()

      progress.value = '0%'
    }

    let ws = {}


    const parseResData = (str) => {
      let code, data

      // like this ->  code=0&data="state=upload&offset=123"
      // like this ->  code=0&data="state=ready&deviceType=esp32"
      const regex = /^code=-?\d*&data=\".*\"/gm

      if (regex.exec(str) == null) {
        return null // can not parse
      }

      // get code between "code=" "&"
      let a, b
      for (let i = 0; i < str.length; i++) {
        const c = str[i]
        if (c == '=') {
          a = i + 1
        } else if (c == '&') {
          b = i
          break
        }
      }

      code = Number(str.slice(a, b))

      // quotes trim
      for (let i = b; i < str.length; i++) {
        if (str[i] == '=') {
          a = i + 2
          break
        }
      }
      b = -1

      data = qs.parse(str.slice(a, b))

      return {
        code: code,
        data: data,
      }
    }

    const otaMsgProcess = (msg) => {
      if (msg.code != 0) {
        // failed
        ota.state = 'connect'
        ota.errMsg = msg.data.msg
        ws.close()
        return
      }

      if (!msg.data) {
        return
      }

      if (msg.data.deviceType) {
        ota.deviceType = msg.data.deviceType
      }

      let offset = msg.data.offset
      if (offset) {
        let newProgress = Math.floor((100 * offset) / ota.totalSize)
        progress.value = `${newProgress}%`
      }

      // state process
      let state = msg.data.state
      if (state == 'ready') {
        readFileCurChunk()
      } else if (state == 'done') {
        // TODO:verify
        progress.value = `0%`
        ota.state = 'done'
      } else if (state == 'cancel') {
        // nothing to do
      }
    }

    const checkImageIsValid = async (file) => {
      const magic_byte = 0xE9

      if (!file) {
        return false
      }

      if (file.size < 1) {
        return
      }

      let data = await file.slice(0, 1).arrayBuffer()
      let view = new Uint8Array(data)

      return view[0] == magic_byte

    }

    const reqOTAStart = (e) => {
      let data = `op=start&data=${ota.totalSize}`
      ws.send(data)
    }

    const reqOTAStop = (e) => {
      let data = `op=stop&data=`
      ws.send(data)
    }

    const wsOnMsg = (e) => {
      let data = parseResData(e.data)
      if (data == null) {
        ws.close()
        ota.state = 'connect'
        ota.errMsg = 'Unable to parse message'
        return
      }

      otaMsgProcess(data)
    }

    const wsOnOpen = (e) => {
      ota.state = 'upload'
    }

    const wsOnClose = (e) => {
      if (ota.state == 'done' || ota.state == 'connect') {
        // Error event occurs before the close event
        return
      }
      ota.state = 'connect'
      ota.errMsg = 'Connection closed.'
    }

    let wsOnErr = (e) => {
      if (ota.state == 'connecting') {
        ota.state = 'connect'
        ota.errMsg = 'Failed to connect.'
      }
    }

    const readFileCurChunk = () => {
      if (ota.offset < ota.totalSize) {
        ota.reader.readAsArrayBuffer(
          ota.file.slice(ota.offset, ota.offset + ota.chunkSize)
        )
      }
    }

    const onFileReaderLoad = (e) => {
      console.log('load!')
      ws.send(ota.reader.result)

      ota.offset += ota.chunkSize
    }

    const onAddressBtn = (e) => {
      e.preventDefault()
      loadSuccessAudio()

      if (ota.state == 'connecting') {
        return
      }
      resetOTA()

      ota.state = 'connecting'
      ws = new WebSocket(`ws://${serverInfo.address}`)
      ws.onopen = wsOnOpen
      ws.onmessage = wsOnMsg
      ws.onclose = wsOnClose
      ws.onerror = wsOnErr
    }

    const onUploadBtn = async (e) => {
      e.preventDefault()

      if (ota.state == 'upload') {
        resetOTA()

        ota.file = files.value[0]

        let isValid = await checkImageIsValid(ota.file)
        if (!isValid) {
          ota.errMsg = 'Invalid firmware, Please try again'
          return
        }

        ota.state = 'uploading'
        ota.totalSize = ota.file.size
        ota.chunkSize = Math.min(defaultChunkSize, Math.floor(ota.totalSize / 10))
        ota.chunkSize = ota.chunkSize < 1 ? 1 : ota.chunkSize // Firmware too small...

        ota.reader.onload = onFileReaderLoad
        // start
        reqOTAStart()
      } else if (ota.state == 'uploading') {
        ota.state = 'upload'
        ota.errMsg = 'user cancel'
        reqOTAStop()
      }
    }



    let successAudio = document.createElement('audio')

    const playSuccessAudio = () => {
      successAudio.play()
    }

    const loadSuccessAudio = () => {
      successAudio.setAttribute('src', '/success.aac')
    }

    // handle the file upload event
    const handleUpload = (e) => {
      files.value = Array.from(e.target.files) || []
    }

    const dropHandler = (e) => {
      e.preventDefault()
      if (ota.state != 'upload') {
        return
      }
      files.value = e.dataTransfer.files
      dragging.value = false
    }

    // only react to actual files being dragged
    const dragEnterHandler = (e) => {
      e.preventDefault()
    }

    return {
      files,
      uploadInfo,
      ota,
      progressType,
      progress,
      dragging,
      serverInfo,
      handleUpload,
      onAddressBtn,
      onUploadBtn,
      dropHandler,
      dragEnterHandler,
      // dragLeaveHandler,
      // dragOverHandler
    }
  },
}
</script>