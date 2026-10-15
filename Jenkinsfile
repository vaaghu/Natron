// Jenkins pipeline: on each push to the stable branch, build Natron and its
// installer with scripts/natron-build.sh.
//
// Job setup (Pipeline job, "Pipeline script from SCM"):
//   Repository URL      git@github.com:vaaghu/Natron.git
//   Branches to build   */stable
//   Script Path         Jenkinsfile
// Run the job once by hand: Jenkins reads the trigger below from this file
// on the first run and then polls on its own.
//
// Jenkins runs on a local machine GitHub cannot reach, so instead of a push
// webhook it polls GitHub every 2 minutes and builds when stable has new
// commits.
//
// The agent needs docker (the jenkins user in the docker group) and rsync.

pipeline {
    agent any

    triggers {
        pollSCM('H/2 * * * *')
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
