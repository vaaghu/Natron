// Jenkins pipeline: on each push to the stable branch, build Natron and its
// installer with scripts/natron-build.sh.
//
// Job setup (Pipeline job, "Pipeline script from SCM"):
//   Repository URL      git@github.com:vaaghu/Natron.git
//   Branches to build   */stable
//   Script Path         Jenkinsfile
//   Build Triggers      "GitHub hook trigger for GITScm polling"
// GitHub webhook (repo Settings -> Webhooks): http://<jenkins>/github-webhook/,
// content type application/json, push events only. Run the job once by hand
// so Jenkins links it to the webhook.
//
// The agent needs docker (the jenkins user in the docker group) and rsync.

pipeline {
    agent any

    triggers {
        githubPush()
    }

    options {
        // natron-build.sh always uses the container name natron-build
        disableConcurrentBuilds()
        buildDiscarder(logRotator(numToKeepStr: '20'))
    }

    environment {
        // Kept outside the workspace so the cache survives between builds
        NATRON_BUILD_CACHE = "${env.JENKINS_HOME}/natron/natron-build-cache"
        NATRON_BUILD_OUT   = "${env.JENKINS_HOME}/natron/builds"
    }

    stages {
        stage('Build') {
            steps {
                // Jenkins checks out a detached HEAD: name the build after the
                // branch and number it like the Jenkins build.
                sh "bash scripts/natron-build.sh all --committed -n stable -b ${env.BUILD_NUMBER}"
            }
        }
    }

    post {
        success {
            echo "Output: ${env.NATRON_BUILD_OUT}/stable/${env.BUILD_NUMBER}"
        }
    }
}
