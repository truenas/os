node('FreeNAS-ISO') {
  properties([disableConcurrentBuilds()])
  stage('Checkout') {
    checkout scm
  }
  withEnv(['GH_ORG=freenas','GH_REPO=os']) {
    stage('ixbuild') {
      echo 'Starting iXBuild Framework pipeline'
      try {
        sh '/ixbuild/jenkins.sh freenas freenas-pipeline'
      } catch (exc) {
        echo 'Saving failed artifacts...'
        archiveArtifacts artifacts: 'artifacts/**', fingerprint: true
        throw exc
      }
    }
    stage('artifact') {
      archiveArtifacts artifacts: 'artifacts/**', fingerprint: true
      junit 'results/**'
    }
  }
}
