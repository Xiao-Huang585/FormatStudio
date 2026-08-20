// ============================================================
// build.gradle.kts (Module: app)
// 方案A: 纯 Java + C++ 实现 YsPlayer（移除 Kotlin 依赖）
//
// 前提:
//   1. 已将 YsPlayer 的 Kotlin 文件替换为 ysplayer_java/ 下的 .java 文件
//   2. YsPlayer 的 C++ 源码已复制到 app/src/main/cpp/ysplayer/
//   3. FFmpeg 静态库已放在 app/src/main/cpp/libs/<abi>/ 目录
//   4. YsPlayer 的 jniLibs (.so) 已放在 app/src/main/jniLibs/<abi>/
// ============================================================

plugins {
    id("com.android.application")
    // 不需要 Kotlin 插件！
    // 如果项目根 build.gradle.kts 中已声明 kotlin-android，可以忽略它
}

android {
    namespace = "com.kgmdecoder.app"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.kgmdecoder.app"
        minSdk = 24
        targetSdk = 34
        versionCode = 1
        versionName = "Test"

        // CMake 配置（在 android.defaultConfig 中声明）
        externalNativeBuild {
            cmake {
                // 传递给 CMake 的参数
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DANDROID_PLATFORM=android-24"
                )

                // 仅编译需要的 ABI（减少编译时间）
                abiFilters += listOf("arm64-v8a")

                // C++ 标准版本
                cppFlags += listOf("-std=c++23", "-fexceptions", "-frtti")
            }
        }

        // NDK 版本（按你本地安装的版本修改）
        ndk {
            abiFilters += listOf("arm64-v8a")
        }
    }

    // ============================================================
    // externalNativeBuild - 指向 CMakeLists.txt
    // ============================================================
    externalNativeBuild {
        cmake {
            // 路径相对于 module 根目录
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    // ============================================================
    // sourceSets - Java 源码目录配置
    // ============================================================
    sourceSets {
        getByName("main") {
            // Java 源码（包含 YsPlayer 的 Java 替代文件）
            java.srcDirs(
                "src/main/java",
                "src/main/kotlin"  // 如果还有 .java 文件放在 kotlin 目录下
            )

            // native 库目录
            jniLibs.srcDirs("src/main/jniLibs")
        }
    }

    // ============================================================
    // 编译选项
    // ============================================================
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    // ============================================================
    // 如果项目中残留 .kt 文件但不编译它们：
    // 使用 packagingOptions 排除 Kotlin 元数据
    // ============================================================
    packaging {
        jniLibs {
            // 避免重复的 .so 文件冲突
            useLegacyPackaging = true
        }

        resources {
            excludes += listOf(
                "META-INF/DEPENDENCIES",
                "META-INF/LICENSE",
                "META-INF/LICENSE.txt",
                "META-INF/license.txt",
                "META-INF/NOTICE",
                "META-INF/NOTICE.txt",
                "META-INF/notice.txt",
                // 排除 Kotlin 元数据（如果第三方库引入了 Kotlin）
                "META-INF/*.kotlin_module",
                "META-INF/kotlin-tooling-metadata.json"
            )
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }
}

// ============================================================
// 依赖项 - 纯 Java，无 Kotlin 标准库
// ============================================================
dependencies {
    implementation("androidx.activity:activity:1.9.0");
    implementation("androidx.fragment:fragment:1.9.0")
    implementation("androidx.constraintlayout:constraintlayout:2.1.4")
    implementation("androidx.appcompat:appcompat:1.6.1")
    implementation("androidx.core:core:1.12.0")

    // 如果 YsPlayer 的 native 库依赖 c++_shared，
    // 它已通过 jniLibs 引入，不需要额外依赖

    // 如果需要 Kotlin Coroutines（仅当你的代码用到了协程），
    // 取消注释；但如果完全用 Java，不需要以下依赖：
    // implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.3")
}

// ============================================================
// 注意事项:
//
// 1. 根项目的 build.gradle.kts 中如果有 classpath("org.jetbrains.kotlin:kotlin-gradle-plugin:...")
//    可以保留（不会影响纯 Java 模块），也可以移除以减小构建体积。
//
// 2. 如果 settings.gradle.kts 中声明了:
//    plugins { id("org.jetbrains.kotlin.android") apply false }
//    这不会导致 Kotlin 代码被编译，因为本模块未 apply 该插件。
//
// 3. 如果之前有 .kt 文件在 src/main/kotlin/ 目录，
//    要么删除它们（因为 Java 替代版已写好），
//    要么将 java.srcDirs 中的 "src/main/kotlin" 移除。
//
// 4. CMakeLists.txt 中的 YsPlayer C++ 源文件不需要修改，
//    JNI 的方法名只要匹配 Java 类的包名/类名即可。
// ============================================================
