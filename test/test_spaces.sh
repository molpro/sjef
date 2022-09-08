#!/bin/sh
molpro_command="/usr/local/bin/molpro"
host=$(hostname)
#host=pjkmarat.chemy.cf.ac.uk
debug=0
work_directory="${TMPDIR:-/tmp}/sjef test spaces"
cache="/tmp/${USER}/sjef test spaces/with spaces"
project_name="project name containing spaces"

# no need to change below here
echo "Working directory: ${work_directory}"
echo "Remote cache: ${host}:${cache}"
echo "Project name: ${project_name}"
rm -rf "${work_directory}"
export SJEF_CONFIG="${work_directory}/dot-sjef"
mkdir -p "${SJEF_CONFIG}/molpro"
cat << EOF >"${SJEF_CONFIG}/molpro/backends.xml"
<?xml version="1.0"?>
<backends>
  <backend name="local"
           run_command="molpro {-n %n!MPI size} {-M %M!Total memory} {-m %m!Process memory} {-G %G!GA memory}"
  />
 <backend name="testing-remote" run_command="${molpro_command}" host="$host" cache="${cache}"/>
</backends>
EOF

project="${work_directory}/${project_name}.molpro"
cat << EOF > "${work_directory}/${project_name}.inp"
geometry={He}
rhf
EOF

sjef new "${project}"
sjef import "${project}" "${work_directory}/${project_name}.inp"
sjef run -b testing-remote "${project}"
if [ $debug -gt 0 ]; then
echo Project: ${project}; ls -lR "${project}"
echo Cache: ${cache}; ssh ${host} ls -lR "\"${cache}\""
fi
sjef status "${project}"
sjef wait "${project}"
#sleep 5
if [ $debug -gt 0 ]; then
echo Cache: ${cache}; ssh ${host} ls -lR "\"${cache}\""
echo Project: ${project}; ls -lR "${project}"
fi
sjef status "${project}"
grep ! "${project}/run/1.molpro/1.out"  ||  ( \
cat "${project}/run/1.molpro/1.out" "${project}/run/1.molpro/1.xml"; \
echo "Job standard error:"; \
cat "${project}/run/1.molpro/1.stderr"; \
echo "Job standard output:" "${project}/run/1.molpro/1.stdout"; \
cat "${project}/run/1.molpro/1.stdout" \
)

if [ $debug -eq 0 ]; then
ssh ${host} rm -rf "\"${cache}\""
rm -rf ${workdir}
fi
