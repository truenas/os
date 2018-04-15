pipeline {
  agent none

  environment {
    GH_ORG = 'freenas'
    GH_REPO = 'os'
  }
  stages {

    stage('Queued') {
      agent {
        label 'JenkinsMaster'
      }
      steps {
        echo "Build queued"
      }
    }

    stage('Makefile Test') {
      agent {
        label 'FreeNAS-ISO'
      }
      steps {
        checkout scm
        echo 'Performing Makefile Checking'
        sh 'make clean'
      }
    }
  }
}
