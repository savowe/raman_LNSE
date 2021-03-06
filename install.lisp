#!/usr/bin/sbcl --script
#|-*- mode:lisp -*-|#
;;;; talises-install.lisp

(require "asdf")
(require "sb-posix")

(defparameter *argv* (uiop:command-line-arguments))
(defun get-arg (arg)
  "Get next command line argument after arg"
  (let ((pos (position arg *argv* :test #'equalp)))
    (when pos
      (elt *argv* (1+ pos)))))

(defun arg-exist-p (arg)
  "Checks if arg is supplied as command line argument"
  (find arg *argv* :test #'string=))

(defparameter *CC* (unless (string= (uiop:getenv "CC") "")
                     (uiop:getenv "CC")))
(defparameter *CXX* (unless (string= (uiop:getenv "CXX") "")
                      (uiop:getenv "CXX")))
(defparameter *debug-mode* (or (arg-exist-p "--debug") (arg-exist-p "-d")))
(defparameter *root-p*
  (string= "0" (string-trim '(#\Space #\Tab #\Newline #\Return #\Linefeed)
                            (uiop:run-program "id -u" :output 'string))))
(defparameter *talises-dir* (uiop:getcwd))
(defparameter *default-system-build-dir* (pathname "/tmp/"))
(defparameter *default-user-build-dir* (merge-pathnames "local/src/" (user-homedir-pathname)))
(defparameter *build-dir* (or (when (get-arg "--build-dir")
                                (uiop:ensure-directory-pathname
                                 (uiop:truenamize (get-arg "--build-dir"))))
                              (if *root-p*
                                  *default-system-build-dir*
                                  *default-user-build-dir*)))
(defparameter *default-system-install-dir* (pathname "/opt/"))
(defparameter *default-user-install-dir*
  (merge-pathnames "local/opt/" (user-homedir-pathname)))
(defparameter *install-dir* (or (when (get-arg "--install-dir")
                                  (uiop:ensure-directory-pathname
                                   (uiop:truenamize (get-arg "--install-dir"))))
                                (if *root-p*
                                    *default-system-install-dir*
                                    *default-user-install-dir*)))
(defparameter *default-system-module-dir* (pathname "/usr/local/share/modules/modulefiles/"))
(defparameter *default-user-module-dir* (merge-pathnames "local/modules/modulefiles/"
                                                         (user-homedir-pathname)))
(defparameter *module-dir* (or (when (get-arg "--module-dir")
                                 (uiop:ensure-directory-pathname
                                  (uiop:truenamize (get-arg "--module-dir"))))
                               (if *root-p*
                                   *default-system-module-dir*
                                   *default-user-module-dir*)))
(defparameter *make-threads* (or (get-arg "-j") 1))
(defparameter *packages* nil)
(defparameter *non-interactive-p* (arg-exist-p "-n"))

(defun download (url)
  (run (format nil "curl -LO ~a" url)))
(defun tar-extract (file)
  (run (format nil "tar xfz ~a" file)))
(defun remove-tgz-filetype (file)
  (subseq file 0 (or (search ".tar.gz" file) (search ".tgz" file) (search ".zip" file))))
(defun make (&key threads arg)
  (run (format nil "make~@[ -j ~a~]~@[ ~a~]" threads arg)))
(defun pathname-type= (pathspec type)
  (string= (pathname-type pathspec) type))
(defun package-match (key value)
  "Returns first plist in *packages* with <key> corresponds to <value>.
Otherwise returns nil. Test is done via equal."
  (find-if (lambda (x) (equal (getf x key) value)) *packages*))

(defun run (cmd &key (ignore-error-status nil))
  (format t "~a ~~~a ~a~%" (uiop:getcwd) (if *root-p* "#" "$") cmd)
  (uiop:run-program (format nil "~a" cmd)
                    :output :interactive
                    :error-output :interactive
                    :ignore-error-status ignore-error-status))

(defun export-variables (full-name)
  (sb-posix:setenv "PATH"
                   (format nil "~a~a/bin/:~a" *install-dir* full-name
                           (sb-posix:getenv "PATH"))
                   1)
  (sb-posix:setenv "LD_LIBRARY_PATH" (format nil "~a~a/lib/:~:*~:*~a~a/lib64/:~a"
                                             *install-dir* full-name
                                             (sb-posix:getenv "LD_LIBRARY_PATH"))
                   1))

(defun get-cpu-flags ()
  (let* ((cpuinfo (uiop:run-program "cat /proc/cpuinfo" :output 'string))
         (flags-start (+ (search "flags		: " cpuinfo)
                         (length "flags		: "))))
    (subseq cpuinfo flags-start (search (string #\Newline)
                                        cpuinfo
                                        :start2 flags-start))))

(defun check-binary-exist (binary &optional optional-p)
  (let ((exit-code
         (nth-value 2 (uiop:run-program (concatenate 'string "which " binary)
                                        :ignore-error-status t))))
    (if (= exit-code 0)
        t
        (format t "~@[Optional ~*~]Dependency missing: ~a ~%" optional-p binary))))

(defun lib-exist-p (library directory)
  (cond ((listp directory)
         (some (lambda (dir) (lib-exist-p library dir))
               directory))
        ((pathnamep (pathname directory))
         (when (< 0
                  (length (uiop:run-program (format nil "find ~a -name ~a\\*"
                                                    directory library)
                                            :output 'string
                                            :ignore-error-status t)))
           t))
        (t (error "Unknown type of directory: ~a ~%~a" (type-of directory) directory))))

(defun check-library-exist (library &optional optional-p)
  (let* ((lib (some (lambda (dir)
                      (lib-exist-p library dir))
                    (append (uiop:split-string (uiop:getenv "LD_LIBRARY_PATH")
                                               :separator (list #\:))
                            (list "/lib/" "/lib64/" "/usr/lib/"
                                  "/usr/lib64/" "/usr/local/lib/"
                                  "/usr/local/lib64/")))))
    (if lib
        t
        (format t "~@[Optional ~*~]Dependency missing: ~a ~%" optional-p library))))

(defclass software ()
  ((name :initarg :name
         :initform (error ":name must be specified")
         :accessor name
         :documentation "Software Name")
   (version :initarg :version
            :initform (error ":version must be specified")
            :accessor version
            :documentation "Software Version")
   (full-name :initarg :full-name
              :initform nil
              :accessor full-name
              :documentation "Full name of software")
   (url :initarg :url
        :initform (error ":url must be specified")
        :accessor url
        :documentation "Url where source code is located")
   (env-variables :initarg :env-variables
                  :initform nil
                  :accessor env-variables
                  :documentation "List of additional environment variables and their values.")))

(defmacro define-software (name &key version url env-vars)
  `(progn
     (defclass ,name (software) ())
     (push (make-instance ',name
                          :name (string-downcase (symbol-name ',name))
                          :version ,version
                          :full-name (concatenate 'string (string-downcase (symbol-name ',name)) "-" ,version)
                          :url ,url
                          :env-variables ,env-vars)
           *packages*)))

(define-software gsl
    :version "2.5"
    :url "ftp://ftp.gnu.org/gnu/gsl/gsl-2.5.tar.gz")
(define-software muparser
    :version "2.2.6.1"
    :url "https://github.com/beltoforion/muparser/archive/v2.2.6.1.tar.gz")
(define-software fftw
    :version "3.3.8"
    :url "http://fftw.org/fftw-3.3.8.tar.gz")
(define-software talises
    :version "git")
(setf *packages* (reverse *packages*))

(defmethod fetch ((sw software))
  (with-slots (name version url) sw
    (download url)
    (if (search ".zip" url) (run (format nil "unzip ~a" (file-namestring url))) (run (format nil "tar xfz ~a" (file-namestring url))))))

(defmethod install-module ((sw software))
  (%install-module sw))


;; gsl
(defmethod install ((sw gsl))
  "Install method for gsl"
  (with-slots (full-name url) sw
    (uiop:with-current-directory ((uiop:ensure-directory-pathname (remove-tgz-filetype (file-namestring url))))
      (run (format nil "./configure --prefix=~a~a" *install-dir* full-name))
      (run "make clean")
      (run (format nil "make ~@[-j ~a~] CFLAGS=\"-march=native -O3\"" *make-threads*))
      (run "make install")
      (export-variables full-name))))

;; muparser
(defmethod install ((sw muparser))
  "Install method for muparser"
  (with-slots (full-name url) sw
    (uiop:with-current-directory
        ((uiop:ensure-directory-pathname full-name))
      (run (format nil "./configure --prefix=~a~a" *install-dir* full-name))
      (run "make clean")
      (run (format nil "make CFLAGS=\"-march=native -std=gnu++11\" CPPFLAGS=\"-march=native -std=gnu++11\" LDFLAGS=\"-march=native -std=gnu++11\""))
      (run "make install")
      (export-variables full-name))))

;; fftw
(defmethod install ((sw fftw))
  "Install method for fftw"
  (let ((full-name (full-name sw))
        (url (url sw))
        (cpu-flags (get-cpu-flags)))
    (uiop:with-current-directory
        ((uiop:ensure-directory-pathname (remove-tgz-filetype (file-namestring url))))
      (run (format nil "./configure --prefix=~a~a --enable-openmp --enable-threads~@[ --enable-sse2~*~]~@[ --enable-avx~*~]~@[ --enable-fma~]"
                   *install-dir*
                   full-name
                   (search " sse2 " cpu-flags)
                   (search " avx " cpu-flags)
                   (search " fma " cpu-flags)))
      (run "make clean")
      (run (format nil "make ~@[-j ~a~]" *make-threads*))
      (run (format nil "make install"))
      (export-variables full-name))))

;; talises
(defmethod fetch ((sw talises))
  nil)
(defmethod install-module ((sw talises))
  (%install-module sw
                   :dependencies
                   (loop :for package :in *packages*
                      :unless (string= (name package) (name sw))
                      :collect (format nil "~a-~a"
                                       (string-downcase (name package))
                                       (version package)))))
(defmethod install ((sw talises))
  "Install method for talises"
  (with-slots (full-name) sw
    (uiop:with-current-directory (*talises-dir*)
      (uiop:chdir (uiop:ensure-pathname (format nil "~abuild/" (uiop:getcwd))
                                        :ensure-directory t
                                        :ensure-directories-exist t))
      (run (format nil "cmake ~@[-DCMAKE_C_COMPILER=~a~] ~@[-DCMAKE_CXX_COMPILER=~a~] .." *CC* *CXX*))
      (run "make clean")
      (run (format nil "make ~@[-j ~a~]" *make-threads*))
        )))

(defun %install-module (sw
                        &optional &key dependencies variables)
  (let* ((tool (name sw))
         (version (version sw))
         (full-name (full-name sw))
         (tool-path (merge-pathnames full-name *install-dir*)))
    (with-open-file (out (merge-pathnames full-name
                                          (ensure-directories-exist *module-dir*))
                         :direction :output :if-exists :supersede
                         :if-does-not-exist :create)
      (format t "Install modulefiles for ~a in ~a~%" full-name *module-dir*)
      (format out "#%Module1.0#####################################################################
################################################################################

set path       ~a
set tool       ~a
set version    ~a

proc ModulesHelp { } {
  puts stderr \"\t $tool $version \"
}

module-whatis  \"sets the environment for $tool $version.\"

################################################################################
#

set mode [ module-info mode ]

if { $mode eq \"load\" || $mode eq \"switch2\" } {
  puts stderr \"Module for $tool $version loaded.\"
} elseif { $mode eq \"remove\" || $mode eq \"switch3\" } {
  puts stderr \"Module for $tool $version unloaded.\"
}

################################################################################
#

~{~&module load ~a~}

prepend-path PATH            $path/bin
prepend-path MANPATH         $path/share/man
prepend-path LD_LIBRARY_PATH $path/lib
prepend-path LD_LIBRARY_PATH $path/lib64
prepend-path LD_RUN_PATH     $path/lib
prepend-path LD_RUN_PATH     $path/lib64
~{~&setenv ~a ~a~}~&"
              tool-path
              tool
              version
              dependencies
              variables))))

(defun display-logo ()
  ;; Banner3
  (format t "~a~%~%" "
  _________    __    _________ ___________
 /_  __/   |  / /   /  _/ ___// ____/ ___/
  / / / /| | / /    / / \\__ \\/ __/  \\__ \\ 
 / / / ___ |/ /____/ / ___/ / /___ ___/ / 
/_/ /_/  |_/_____/___//____/_____//____/  "))

(defun display-help ()
  (format t "Options:
-h, --help            Print this help text
--no-fetch            Skip Download
--no-modules          Skip Module file installation
--no-install          Skip Installation
-j THREADS            Number of make threads
--build-dir DIR       Build directory
                          System Default: ~a
                          User Default:   ~a
--install-dir DIR     Installation directory
                          System Default: ~a
                          User Default:   ~a
--module-dir DIR      Module directory
                          System Default: ~a
                          User Default:   ~a
-n PACKAGES           Non-interactive Mode
                          Install PACKAGES~%~%"
          *default-system-build-dir* *default-user-build-dir*
          *default-system-install-dir* *default-user-install-dir*
          *default-system-module-dir*  *default-user-module-dir*))

(defun check-dependencies ()
  (format t "Check Dependencies.~%")
  (unless (and (every #'check-binary-exist
                      (list "gcc"
                            "g++"
                            "make"
                            "cmake")))
    (error "Error: Dependency missing.~%" )
    (uiop:quit)))

(defun display-current-configuration ()
  (format t "Current Configuration (see ./install -h):~%")
  (format t "    Current Working Directory: ~a~%" (uiop:getcwd))
  (format t "    Software will be build in: ~a~%" *build-dir*)
  (format t "    Software will be installed in: ~a~%" *install-dir*)
  (format t "    Modulefiles will be installed in: ~a~%" *module-dir*)
  (format t "    Make Threads: ~a~%" *make-threads*)
  (format t "    CC=~a, CXX=~a~%" *CC* *CXX*)
  (when *debug-mode*
    (format t "    Debug Mode: ON~%" )))

(defun collect-selected-packages (selection)
  (loop :for package :in *packages*
     :for index :below (length *packages*)
     :when (or (find #\a selection) (find (digit-char index) selection))
     :collect package))

(defun update-shell-config ()
  (let ((module-path (format nil "export MODULEPATH=~a:$MODULEPATH" *module-dir*))
        (shell-config-file (if (string= (pathname-name (uiop:getenv "SHELL")) "zsh")
                               ".zshrc"
                               ".bashrc")))
    (when (or *non-interactive-p*
              (yes-or-no-p "Should I append the following text to your ~a?~%~a~%"
                           shell-config-file module-path))
      (with-open-file (out (merge-pathnames shell-config-file
                                            (user-homedir-pathname))
                           :direction :output
                           :if-exists :append
                           :if-does-not-exist :create)
        (format out "~&~a~%" module-path))
      (format t "Shell config update."))))

(defun main ()
  (display-logo)
  (display-help)
  (when (or (arg-exist-p "-h") (arg-exist-p "--help"))
    (uiop:quit))

  (mapcar #'ensure-directories-exist (list *build-dir* *install-dir* *module-dir*))

  (check-dependencies)
  (display-current-configuration)

  (format t "~%What do you want to install?
Press number of each package to be installed and then press ENTER:
~{~&~a - ~a~20t (~a)~}
a - all
q - Abort Installation.
>> " (loop :for package :in *packages*
        :for index :below (length *packages*)
        :append (list index (string-downcase (name package)) (string-downcase (version package)))))

  (let* ((selection (if *non-interactive-p*
                        (get-arg "-n")
                        (read-line t nil)))
         (requested-packages (collect-selected-packages selection)))
    ;;;; Test for Quit condition
    (when (or (null selection) (find #\q selection))
      (fresh-line)
      (uiop:quit))

    (uiop:with-current-directory (*build-dir*)
      (unless (arg-exist-p "--no-fetch")
        (mapcar #'fetch requested-packages))
      (unless (arg-exist-p "--no-modules")
        (mapcar #'install-module requested-packages))
      (unless (arg-exist-p "--no-install")
        (mapcar #'install requested-packages)))

    (format t "~%Installation finished.~%")

    (unless (or (arg-exist-p "--no-modules") *non-interactive-p*)
      (update-shell-config))))

(with-open-file (log "install.log" :direction :output :if-does-not-exist :create :if-exists :supersede)
  (let ((*standard-output* (make-broadcast-stream *standard-output* log)))
    (main)
    (uiop:quit)))
